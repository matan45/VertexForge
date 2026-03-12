#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

// Shadow pass for grass - simplified blade geometry (depth only)
// Culls grass chunks against light frustum, generates depth-only output

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform GrassShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    float maxShadowDistance;
    float alphaClipThreshold;
} pc;

struct GrassInstanceGPU {
    vec4 positionScale;     // xyz = world position, w = scale
    vec4 rotationData;      // xyz = rotation angles, w = species ID
};

layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    GrassInstanceGPU instances[];
};

struct GrassShadowPayload {
    uint instanceIndices[32];
    uint count;
};

taskPayloadSharedEXT GrassShadowPayload payload;

shared uint sharedVisibleCount;
shared uint sharedIndices[32];
shared vec4 sharedFrustumPlanes[6];

bool sphereInFrustum(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        float d = dot(sharedFrustumPlanes[i].xyz, center) + sharedFrustumPlanes[i].w;
        if (d < -radius) return false;
    }
    return true;
}

void main() {
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;

        mat4 vp = pc.lightViewProjection;
        sharedFrustumPlanes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
        sharedFrustumPlanes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
        sharedFrustumPlanes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
        sharedFrustumPlanes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
        sharedFrustumPlanes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
        sharedFrustumPlanes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

        const float eps = 0.0001;
        for (int i = 0; i < 6; i++) {
            float len = max(length(sharedFrustumPlanes[i].xyz), eps);
            sharedFrustumPlanes[i] /= len;
        }
    }
    barrier();

    uint baseIndex = gl_WorkGroupID.x * 32 + gl_LocalInvocationID.x;

    if (baseIndex < pc.instanceCount) {
        GrassInstanceGPU inst = instances[baseIndex];
        vec3 pos = inst.positionScale.xyz;
        float scale = inst.positionScale.w;

        // Approximate grass blade as sphere for culling
        float bladeRadius = scale * 1.5;

        if (sphereInFrustum(pos, bladeRadius)) {
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < 32) {
                sharedIndices[slot] = baseIndex;
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visCount = min(sharedVisibleCount, 32u);
        payload.count = visCount;
        for (uint i = 0; i < visCount; i++) {
            payload.instanceIndices[i] = sharedIndices[i];
        }
        EmitMeshTasksEXT(visCount, 1, 1);
    }
}

#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

layout(push_constant) uniform GrassShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    float maxShadowDistance;
    float alphaClipThreshold;
} pc;

struct GrassInstanceGPU {
    vec4 positionScale;
    vec4 rotationData;
};

layout(std430, set = 0, binding = 0) readonly buffer GrassInstanceBuffer {
    GrassInstanceGPU instances[];
};

struct GrassShadowPayload {
    uint instanceIndices[32];
    uint count;
};

taskPayloadSharedEXT GrassShadowPayload payload;

void main() {
    uint payloadIndex = gl_WorkGroupID.x;

    if (payloadIndex >= payload.count) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint instanceIndex = payload.instanceIndices[payloadIndex];
    GrassInstanceGPU inst = instances[instanceIndex];

    vec3 basePos = inst.positionScale.xyz;
    float scale = inst.positionScale.w;
    float rotation = inst.rotationData.x;

    float bladeWidth = 0.04 * scale;
    float bladeHeight = 0.6 * scale;

    float cosR = cos(rotation);
    float sinR = sin(rotation);

    vec3 right = vec3(cosR, 0.0, sinR) * bladeWidth;
    vec3 up = vec3(0.0, bladeHeight, 0.0);

    vec3 v0 = basePos - right;
    vec3 v1 = basePos + right;
    vec3 v2 = basePos + up;

    SetMeshOutputsEXT(3, 1);

    gl_MeshVerticesEXT[0].gl_Position = pc.lightViewProjection * vec4(v0, 1.0);
    gl_MeshVerticesEXT[1].gl_Position = pc.lightViewProjection * vec4(v1, 1.0);
    gl_MeshVerticesEXT[2].gl_Position = pc.lightViewProjection * vec4(v2, 1.0);

    gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
