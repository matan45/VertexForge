#type TASK
#version 460
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"

layout(local_size_x = 32) in;

struct BillboardInstance {
    vec4 positionAndScale;    // xyz = world position, w = uniform scale
    vec4 atlasUVRect;         // static: xy=UV offset, zw=UV size. animated: xy=scrollU/V
    vec4 colorTint;           // rgba
    uint bindlessTextureIndex;
    uint flags;
    uint entityId;
    float rotation;           // static: radians. animated: spin rate (rad/sec)
    vec2 size;                // width, height in world units
    float flipbookColsRows;   // encoded floor(cols)*256 + rows (animated only)
    float flipbookFrameRate;  // frames/sec (animated only)
};

layout(std430, set = 0, binding = 0) readonly buffer BillboardInstanceBuffer {
    BillboardInstance instances[];
};

layout(std430, set = 0, binding = 1) readonly buffer BillboardCountBuffer {
    uint instanceCount;
};

layout(set = 1, binding = 0) uniform CameraUBO {
    GPUCameraData camera;
};

struct BillboardPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT BillboardPayload payload;

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint globalIdx = gl_WorkGroupID.x * 32 + tid;

    bool visible = false;

    if (globalIdx < instanceCount) {
        BillboardInstance inst = instances[globalIdx];
        vec3 worldPos = inst.positionAndScale.xyz;
        float maxDim = max(inst.size.x, inst.size.y) * inst.positionAndScale.w;
        float boundRadius = maxDim * 0.707; // ~sqrt(2)/2 for billboard diagonal

        visible = sphereInFrustum(vec4(worldPos, boundRadius), camera.frustumPlanes);

        if (visible) {
            float dist = distance(worldPos, camera.cameraPosition.xyz);
            visible = (dist < camera.farPlane);
        }
    }

    uvec4 ballot = subgroupBallot(visible);
    uint count = subgroupBallotBitCount(ballot);

    // Compact visible instance indices into contiguous payload slots
    // so mesh work group N reads payload.instanceIndices[N]
    if (visible) {
        uint compactIdx = subgroupBallotExclusiveBitCount(ballot);
        payload.instanceIndices[compactIdx] = globalIdx;
    }

    if (tid == 0) {
        EmitMeshTasksEXT(count, 1, 1);
    }
}
