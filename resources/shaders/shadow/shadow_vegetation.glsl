#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

// Shadow pass for tree/vegetation meshes (LOD0/LOD1)
// Frustum culls vegetation instances against light frustum, depth-only output

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint MAX_VISIBLE = 128;

layout(push_constant) uniform VegetationShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    float maxShadowDistance;
    float depthBias;
    float slopeBias;
} pc;

struct TreeInstanceGPU {
    vec4 positionScale;     // xyz = world position, w = uniform scale
    vec4 rotationSpecies;   // xyz = rotation, w = species ID (as float)
    vec4 lodData;           // x = LOD level, y = fade factor, zw = reserved
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstanceGPU instances[];
};

layout(std430, set = 1, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

struct VegetationShadowPayload {
    uint instanceIndices[MAX_VISIBLE];
    uint count;
};

taskPayloadSharedEXT VegetationShadowPayload payload;

shared uint sharedVisibleCount;
shared uint sharedIndices[MAX_VISIBLE];
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
        TreeInstanceGPU inst = instances[baseIndex];
        vec3 pos = inst.positionScale.xyz;
        float scale = inst.positionScale.w;

        // Only render LOD0 and LOD1 for shadows (skip imposters)
        uint lod = uint(inst.lodData.x);
        if (lod <= 1) {
            // Approximate tree as sphere for culling
            float treeRadius = scale * 5.0;

            if (sphereInFrustum(pos, treeRadius)) {
                uint slot = atomicAdd(sharedVisibleCount, 1);
                if (slot < MAX_VISIBLE) {
                    sharedIndices[slot] = baseIndex;
                }
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visCount = min(sharedVisibleCount, MAX_VISIBLE);
        payload.count = visCount;
        for (uint i = 0; i < visCount; i++) {
            payload.instanceIndices[i] = sharedIndices[i];
        }
        EmitMeshTasksEXT(visCount > 0 ? 1 : 0, 1, 1);
    }
}

#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

// Placeholder mesh shader for vegetation shadow pass
// In the full implementation, this would read meshlet data from the merged mesh buffer
// and transform vertices using the instance's model matrix

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;
const uint MAX_VISIBLE = 128;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(push_constant) uniform VegetationShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    float maxShadowDistance;
    float depthBias;
    float slopeBias;
} pc;

struct TreeInstanceGPU {
    vec4 positionScale;
    vec4 rotationSpecies;
    vec4 lodData;
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstanceGPU instances[];
};

layout(std430, set = 1, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 1, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 1, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 2, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

struct VegetationShadowPayload {
    uint instanceIndices[MAX_VISIBLE];
    uint count;
};

taskPayloadSharedEXT VegetationShadowPayload payload;

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

mat4 buildModelMatrix(vec3 pos, vec3 rot, float scale) {
    float cx = cos(rot.x), sx = sin(rot.x);
    float cy = cos(rot.y), sy = sin(rot.y);
    float cz = cos(rot.z), sz = sin(rot.z);

    mat3 rotMatrix = mat3(
        cy*cz, cy*sz, -sy,
        sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, sx*cy,
        cx*sy*cz + sx*sz, cx*sy*sz - sx*cz, cx*cy
    );

    mat4 m = mat4(1.0);
    m[0] = vec4(rotMatrix[0] * scale, 0.0);
    m[1] = vec4(rotMatrix[1] * scale, 0.0);
    m[2] = vec4(rotMatrix[2] * scale, 0.0);
    m[3] = vec4(pos, 1.0);
    return m;
}

void main() {
    // Placeholder: actual meshlet-based rendering will be connected
    // when VegetationMeshShaderPipeline is fully implemented
    SetMeshOutputsEXT(0, 0);
}
