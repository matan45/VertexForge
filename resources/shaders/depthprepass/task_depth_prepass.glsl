#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

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

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];

void main() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];
    mat4 modelMatrix = drawData.modelMatrix;

    uint meshletOffset = drawData.meshletOffset;
    uint meshletCount = drawData.meshletCount;

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

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
