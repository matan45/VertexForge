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

layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;
    vec4 cameraPosition;     // .w = nearPlane
    vec4 screenParams;
    vec4 frustumPlanes[6];
    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    float time;
};

struct BillboardPayload {
    uint instanceIndices[32];
};

taskPayloadSharedEXT BillboardPayload payload;

bool isInsideFrustum(vec3 center, float radius) {
    for (int i = 0; i < 6; ++i) {
        if (dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint globalIdx = gl_WorkGroupID.x * 32 + tid;

    bool visible = false;

    if (globalIdx < instanceCount) {
        BillboardInstance inst = instances[globalIdx];
        vec3 worldPos = inst.positionAndScale.xyz;
        float maxDim = max(inst.size.x, inst.size.y) * inst.positionAndScale.w;
        float boundRadius = maxDim * 0.707; // ~sqrt(2)/2 for billboard diagonal

        visible = isInsideFrustum(worldPos, boundRadius);

        // Distance culling (use far plane as max render distance)
        if (visible) {
            float dist = distance(worldPos, cameraPosition.xyz);
            visible = (dist < farPlane);
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
