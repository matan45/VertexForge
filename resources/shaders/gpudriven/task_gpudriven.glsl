#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 1, binding = 1) readonly buffer InstanceTransformBuffer {
    mat4 instanceTransforms[];
};

layout(std430, set = 1, binding = 2) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

// Must match MeshletCullingStats in MeshShaderPipeline.hpp
layout(std430, set = 3, binding = 3) buffer CullingStatsBuffer {
    uint totalMeshlets;
    uint culledByFrustum;
    uint culledByBackface;
    uint visibleMeshlets;
} stats;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

const uint MESHLET_CULL_FRUSTUM_BIT = 0x100u;
const uint MESHLET_CULL_BACKFACE_BIT = 0x200u;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
    mat4 instanceModelMatrix;
    mat4 instanceNormalMatrix;
    uint instanceLodLevel;
};

taskPayloadSharedEXT MeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

bool coneCullTest(vec4 cone, mat4 modelMatrix, vec3 cameraPos, vec3 meshletCenter) {
    if (cone.w >= 1.0) {
        return true;
    }
    vec3 worldConeAxis = normalize(mat3(modelMatrix) * cone.xyz);
    vec3 viewDir = normalize(meshletCenter - cameraPos);
    float dotProduct = dot(viewDir, worldConeAxis);
    return dotProduct < cone.w;
}

uvec4 getMeshletLODDataTask(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
}

float projectSphereToScreenTask(vec4 viewSphere, mat4 projection, vec2 screenSize) {
    vec4 clipPos = projection * vec4(viewSphere.xyz, 1.0);
    if (clipPos.w <= 0.01) {
        return 10000.0;
    }
    float projectedRadius = viewSphere.w * abs(projection[1][1]) / clipPos.w;
    float screenDiameter = projectedRadius * screenSize.y;
    return screenDiameter;
}

uint selectLODTask(float screenPixels, vec4 thresholds) {
    float adjustedPixels = screenPixels * pow(2.0, -thresholds.w);
    if (adjustedPixels > thresholds.x) return 0;
    if (adjustedPixels > thresholds.y) return 1;
    if (adjustedPixels > thresholds.z) return 2;
    return 3;
}

uint findBestAvailableLODTask(uint targetLOD, uint availableMask) {
    if (availableMask == 0xFu) return targetLOD;
    if (availableMask == 0u) return 0xFFFFFFFFu;
    for (uint lod = targetLOD; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) return lod;
    }
    for (uint lod = 0u; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) return lod;
    }
    return 0xFFFFFFFFu;
}

const uint FLAG_UNIFORM_SCALE = 1u << 9;
const uint FLAG_INSTANCED     = 1u << 15;

void main() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];

    uint instanceIndex = gl_WorkGroupID.y;
    mat4 modelMatrix;
    bool isInstanced = (drawData.flags & FLAG_INSTANCED) != 0u && drawData.instanceCount > 1u;

    // Per-instance meshlet range (may differ from drawData for instanced objects)
    uint meshletOffset = drawData.meshletOffset;
    uint meshletCount = drawData.meshletCount;
    uint actualLodLevel = drawData.lodLevel;

    if (isInstanced) {
        uint instanceOffset = drawData.instanceData.w;
        modelMatrix = instanceTransforms[instanceOffset + instanceIndex];

        GPUObjectData obj = objects[drawData.objectIndex];

        vec3 localCenter = (obj.aabbMin.xyz + obj.aabbMax.xyz) * 0.5;
        float localRadius = length(obj.aabbMax.xyz - localCenter);
        vec4 worldSphere = transformBoundingSphere(vec4(localCenter, localRadius), modelMatrix);

        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
                if (gl_LocalInvocationID.x == 0) {
                    payload.drawIndex = drawIndex;
                    payload.meshletCount = 0;
                    payload.instanceModelMatrix = modelMatrix;
                    payload.instanceNormalMatrix = mat4(1.0);
                    EmitMeshTasksEXT(0, 1, 1);
                }
                return;
            }
        }

        vec4 viewSphere = vec4((camera.view * vec4(worldSphere.xyz, 1.0)).xyz, worldSphere.w);
        float screenPixels = projectSphereToScreenTask(viewSphere, camera.projection,
            vec2(pc.screenWidth, pc.screenHeight));
        uint targetLOD = selectLODTask(screenPixels, obj.lodThresholds);
        uint lodLevel = findBestAvailableLODTask(targetLOD, obj.availableLODMask);

        if (lodLevel != 0xFFFFFFFFu) {
            uvec4 lodData = getMeshletLODDataTask(obj, lodLevel);
            meshletOffset = lodData.x;
            meshletCount = lodData.y;
            actualLodLevel = lodLevel;

            // Fallback: if selected LOD has 0 meshlets (e.g. mesh too simple
            // to simplify), walk down to find a LOD with actual data.
            while (meshletCount == 0u && actualLodLevel > 0u) {
                actualLodLevel--;
                if ((obj.availableLODMask & (1u << actualLodLevel)) == 0u) {
                    continue;
                }
                lodData = getMeshletLODDataTask(obj, actualLodLevel);
                meshletOffset = lodData.x;
                meshletCount = lodData.y;
            }
        }
    } else {
        modelMatrix = drawData.modelMatrix;
    }

    uint localMeshletIndex = gl_LocalInvocationID.x;
    uint workgroupMeshletBase = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE;
    uint meshletIndex = workgroupMeshletBase + localMeshletIndex;

    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    bool isValidMeshlet = meshletIndex < meshletCount;
    bool isVisible = false;

    if (isValidMeshlet) {
        uint globalMeshletIndex = meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, modelMatrix);

        atomicAdd(stats.totalMeshlets, 1);
        isVisible = true;

        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            bool frustumVisible = sphereInFrustum(worldSphere, camera.frustumPlanes);
            if (!frustumVisible) {
                atomicAdd(stats.culledByFrustum, 1);
                isVisible = false;
            }
        }

        if (isVisible && (pc.viewMode & MESHLET_CULL_BACKFACE_BIT) != 0u) {
            bool backfaceVisible = coneCullTest(meshlet.cone, modelMatrix,
                                                camera.cameraPos, worldSphere.xyz);
            if (!backfaceVisible) {
                atomicAdd(stats.culledByBackface, 1);
                isVisible = false;
            }
        }

        if (isVisible) {
            atomicAdd(stats.visibleMeshlets, 1);
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < TASK_WORKGROUP_SIZE) {
                sharedMeshletIndices[slot] = globalMeshletIndex;
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);
        payload.drawIndex = drawIndex;
        payload.meshletCount = visibleCount;

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        payload.instanceModelMatrix = modelMatrix;

        mat3 modelMat3 = mat3(modelMatrix);
        mat3 normalMat3;
        if ((drawData.flags & FLAG_UNIFORM_SCALE) != 0u) {
            float scale = length(modelMat3[0]);
            normalMat3 = modelMat3 * (1.0 / scale);
        } else {
            normalMat3 = transpose(inverse(modelMat3));
        }
        payload.instanceNormalMatrix = mat4(normalMat3);
        payload.instanceLodLevel = actualLodLevel;

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
