#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];  // 3 vec4s per instance
};

layout(std430, set = 0, binding = 1) readonly buffer GrassCountBuffer {
    uint totalInstances;
};

// Camera — matches CameraUBO (240 bytes): view, projection, cameraPos, time, frustumPlanes[6]
layout(set = 1, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 tipColor;
    float fadeStartDistance;
    float fadeEndDistance;
};

struct GrassPayload {
    uint instanceIndices[32];
    float distanceToCamera[32];
};

taskPayloadSharedEXT GrassPayload payload;

// Frustum cull using pre-normalized planes from UBO (same as gpu_cull_lod / task_gpudriven)
bool sphereInFrustum(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        float d = dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w;
        if (d < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint tid = gl_LocalInvocationID.x;
    uint groupBase = gl_WorkGroupID.x * 32;
    uint instanceIdx = groupBase + tid;

    bool visible = false;
    float dist = 0.0;

    if (instanceIdx < totalInstances) {
        vec4 posAndRot = grassInstances[instanceIdx * 3];
        vec4 dimensions = grassInstances[instanceIdx * 3 + 1];
        vec3 worldPos = posAndRot.xyz;
        float bladeHeight = dimensions.x;

        dist = distance(worldPos, cameraPos);
        if (dist < fadeEndDistance) {
            // Frustum culling with bounding sphere centered at mid-blade
            // Use minimum radius of 2.0 to avoid over-culling small blades at frustum edges
            vec3 sphereCenter = worldPos + vec3(0.0, bladeHeight * 0.5, 0.0);
            float sphereRadius = max(bladeHeight * 0.6, 2.0);

            if (sphereInFrustum(sphereCenter, sphereRadius)) {
                visible = true;
            }
        }
    }

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
