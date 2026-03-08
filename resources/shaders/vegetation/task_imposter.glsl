#type TASK
#version 460
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require

layout(local_size_x = 32) in;

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

struct ImposterPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT ImposterPayload payload;

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint globalIdx = gl_WorkGroupID.x * 32 + tid;

    bool visible = (globalIdx < visibleCount);

    if (visible) {
        payload.instanceIndices[tid] = visibleInstances[globalIdx].instanceIndex;
    }

    uvec4 ballot = subgroupBallot(visible);
    uint count = subgroupBallotBitCount(ballot);

    if (tid == 0) {
        EmitMeshTasksEXT(count, 1, 1);
    }
}
