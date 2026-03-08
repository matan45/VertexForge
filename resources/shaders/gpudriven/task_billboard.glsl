#type TASK
#version 460
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require

layout(local_size_x = 32) in;

struct BillboardInstance {
    vec4 positionAndScale;    // xyz = world position, w = uniform scale
    vec4 atlasUVRect;         // xy = UV offset, zw = UV size
    vec4 colorTint;           // rgba
    uint bindlessTextureIndex;
    uint flags;
    uint entityId;
    float rotation;
    vec2 size;                // width, height in world units
    uint padding[2];
};

layout(std430, set = 0, binding = 0) readonly buffer BillboardInstanceBuffer {
    BillboardInstance instances[];
};

layout(std430, set = 0, binding = 1) readonly buffer BillboardCountBuffer {
    uint instanceCount;
};

struct BillboardPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT BillboardPayload payload;

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint globalIdx = gl_WorkGroupID.x * 32 + tid;

    bool visible = (globalIdx < instanceCount);

    if (visible) {
        payload.instanceIndices[tid] = globalIdx;
    }

    uvec4 ballot = subgroupBallot(visible);
    uint count = subgroupBallotBitCount(ballot);

    if (tid == 0) {
        EmitMeshTasksEXT(count, 1, 1);
    }
}
