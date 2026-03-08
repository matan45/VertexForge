#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// Grass instances from compute pass
layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];  // 3 vec4s per instance
};

layout(std430, set = 0, binding = 1) readonly buffer GrassCountBuffer {
    uint totalInstances;
};

// Camera
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
    float time;
};

// Grass config push constants
layout(push_constant) uniform PushConstants {
    float fadeStartDistance;
    float fadeEndDistance;
};

struct GrassPayload {
    uint instanceIndices[32];
    float distanceToCamera[32];
};

taskPayloadSharedEXT GrassPayload payload;

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint groupBase = gl_WorkGroupID.x * 32;
    uint instanceIdx = groupBase + tid;

    bool visible = false;
    float dist = 0.0;

    if (instanceIdx < totalInstances) {
        // Read instance position
        vec4 posAndRot = grassInstances[instanceIdx * 3];
        vec3 worldPos = posAndRot.xyz;

        // Distance culling only — skip frustum check for now
        dist = distance(worldPos, cameraPos);
        if (dist < fadeEndDistance) {
            visible = true;
        }
    }

    // Compact visible instances
    uvec4 ballot = subgroupBallot(visible);
    uint visibleCount = subgroupBallotBitCount(ballot);
    uint localIdx = subgroupBallotExclusiveBitCount(ballot);

    if (visible) {
        payload.instanceIndices[localIdx] = instanceIdx;
        payload.distanceToCamera[localIdx] = dist;
    }

    if (tid == 0) {
        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
