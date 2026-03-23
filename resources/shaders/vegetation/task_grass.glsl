#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    vec4 grassInstances[];  // 3 vec4s per instance
};

layout(std430, set = 0, binding = 1) readonly buffer GrassCountBuffer {
    uint totalInstances;
};

layout(set = 1, binding = 0) uniform CameraUBO {
    GPUCameraData camera;
};

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 tipColor;
    float fadeStartDistance;
    float fadeEndDistance;
    float sssDistortion;
    float sssPower;
    float sssScale;
    uint billboardTextureIndex;
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
        vec4 posAndRot = grassInstances[instanceIdx * 3];
        vec3 worldPos = posAndRot.xyz;
        vec4 scaleAndDensity = grassInstances[instanceIdx * 3 + 1];
        float grassHeight = scaleAndDensity.x;

        dist = distance(worldPos, camera.cameraPosition.xyz);
        if (dist < fadeEndDistance) {
            vec4 boundingSphere = vec4(worldPos + vec3(0.0, grassHeight * 0.5, 0.0), grassHeight);
            if (sphereInFrustum(boundingSphere, camera.frustumPlanes)) {
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
