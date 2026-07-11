#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

// VK-1490 editor selection mask. Clone of gpudriven/task_gpudriven.glsl (NOT the
// depth-prepass task shader — that one has no instancing path) trimmed to the
// mask pass's needs: a per-object selection-bit early-out, frustum-only meshlet
// culling (no stats/backface/Hi-Z), and the same instanced LOD selection as the
// main pass so the silhouette matches what is on screen.

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

#include "../common/gpu_instance_types.glsl"

layout(std430, set = 1, binding = 1) readonly buffer InstanceTransformBuffer {
    GPUInstanceTransform instanceTransforms[];
};

layout(std430, set = 1, binding = 2) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

// One bit per GPU object slot; only selected objects rasterize into the mask.
layout(std430, set = 6, binding = 0) readonly buffer SelectionBits {
    uint selectionBits[];
};

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

const uint MESHLET_CULL_FRUSTUM_BIT = 0x100u;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
    mat4 instanceModelMatrix;
};

taskPayloadSharedEXT MeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];

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

const uint FLAG_INSTANCED = 1u << 15;

void emitNothing(uint drawIndex) {
    if (gl_LocalInvocationID.x == 0) {
        payload.drawIndex = drawIndex;
        payload.meshletCount = 0u;
        payload.instanceModelMatrix = mat4(1.0);
        EmitMeshTasksEXT(0, 1, 1);
    }
}

void main() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];

    // Selection early-out: non-selected draws emit nothing.
    uint objectIndex = drawData.objectIndex;
    if ((selectionBits[objectIndex >> 5u] & (1u << (objectIndex & 31u))) == 0u) {
        emitNothing(drawIndex);
        return;
    }

    uint instanceIndex = gl_WorkGroupID.y;
    mat4 modelMatrix;
    bool isInstanced = (drawData.flags & FLAG_INSTANCED) != 0u && drawData.instanceCount > 1u;

    uint meshletOffset = drawData.meshletOffset;
    uint meshletCount = drawData.meshletCount;

    if (isInstanced) {
        uint instanceOffset = drawData.instanceData.w;
        GPUInstanceTransform instTransform = instanceTransforms[instanceOffset + instanceIndex];
        modelMatrix = instTransform.modelMatrix;

        GPUObjectData obj = objects[drawData.objectIndex];

        vec3 localCenter = (obj.aabbMin.xyz + obj.aabbMax.xyz) * 0.5;
        float localRadius = length(obj.aabbMax.xyz - localCenter);
        vec4 worldSphere = transformBoundingSphere(vec4(localCenter, localRadius), modelMatrix);

        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
                emitNothing(drawIndex);
                return;
            }
        }

        // Same LOD selection as the main pass so the outline hugs the LOD that
        // is actually on screen.
        vec4 viewSphere = vec4((camera.view * vec4(worldSphere.xyz, 1.0)).xyz, worldSphere.w);
        float instanceScreenPixels = projectSphereToScreenTask(viewSphere, camera.projection,
            vec2(pc.screenWidth, pc.screenHeight));
        uint targetLOD = selectLODTask(instanceScreenPixels, obj.lodThresholds);
        uint lodLevel = findBestAvailableLODTask(targetLOD, obj.availableLODMask);

        if (lodLevel != 0xFFFFFFFFu) {
            uvec4 lodData = getMeshletLODDataTask(obj, lodLevel);
            meshletOffset = lodData.x;
            meshletCount = lodData.y;

            uint fallbackLod = lodLevel;
            while (meshletCount == 0u && fallbackLod > 0u) {
                fallbackLod--;
                if ((obj.availableLODMask & (1u << fallbackLod)) == 0u) {
                    continue;
                }
                lodData = getMeshletLODDataTask(obj, fallbackLod);
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

    if (isValidMeshlet) {
        uint globalMeshletIndex = meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, modelMatrix);

        bool isVisible = sphereInFrustum(worldSphere, camera.frustumPlanes);

        if (isVisible) {
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

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
