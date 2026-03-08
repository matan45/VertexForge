#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;

struct VisibleInstance {
    uint instanceIndex;
    uint lodLevel;
};

layout(std430, set = 0, binding = 0) readonly buffer VisibleBuffer {
    VisibleInstance visibleInstances[];
};

layout(std430, set = 0, binding = 1) readonly buffer VisibleCountBuffer {
    uint visibleCount;
};

// Must match TreeInstanceGPU in VegetationGPUTypes.hpp
struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

layout(std430, set = 0, binding = 2) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
};

layout(set = 1, binding = 0) uniform CameraUBO {
    CameraData camera;
};

struct VegetationPayload {
    uint instanceIndices[32];
    uint lodLevels[32];
    uint instanceCount;
};

taskPayloadSharedEXT VegetationPayload payload;

shared uint sharedVisibleCount;
shared uint sharedInstanceIndices[TASK_WORKGROUP_SIZE];
shared uint sharedLodLevels[TASK_WORKGROUP_SIZE];

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint globalIdx = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE + tid;

    if (tid == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    bool visible = (globalIdx < visibleCount);

    if (visible) {
        uint slot = atomicAdd(sharedVisibleCount, 1);
        sharedInstanceIndices[slot] = visibleInstances[globalIdx].instanceIndex;
        sharedLodLevels[slot] = visibleInstances[globalIdx].lodLevel;
    }
    barrier();

    uint count = sharedVisibleCount;

    // Copy to payload
    if (tid < count) {
        payload.instanceIndices[tid] = sharedInstanceIndices[tid];
        payload.lodLevels[tid] = sharedLodLevels[tid];
    }

    if (tid == 0) {
        payload.instanceCount = count;
        EmitMeshTasksEXT(count, 1, 1);
    }
}
