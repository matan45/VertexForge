#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

// Must match PerDrawData in GPUDrivenTypes.hpp (240 bytes)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint boneMatrixOffset; // Offset into bone SSBO, 0xFFFFFFFF if static
    uint boneCount;        // Number of bones for this object
    uint padding3;
};

// Must match GPUMeshlet in MeshletBufferTypes.hpp (48 bytes)
struct GPUMeshlet {
    uint vertexOffset;
    uint primitiveOffset;
    uint vertexPrimCount;
    uint globalVertexOffset;
    vec4 boundingSphere;
    vec4 cone;
};

// Must match CameraUBO in MeshTypes.hpp
struct CameraData {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
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

void main() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];

    uint localMeshletIndex = gl_LocalInvocationID.x;
    uint workgroupMeshletBase = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE;
    uint meshletIndex = workgroupMeshletBase + localMeshletIndex;

    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    bool isValidMeshlet = meshletIndex < drawData.meshletCount;
    bool isVisible = false;

    if (isValidMeshlet) {
        uint globalMeshletIndex = drawData.meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);

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
            bool backfaceVisible = coneCullTest(meshlet.cone, drawData.modelMatrix,
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

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
