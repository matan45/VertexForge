#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

// Shadow pass for vegetation meshes — frustum cull instances, emit meshlets for depth-only rendering.
// One workgroup processes one instance. Emits meshlet workgroups for visible instances.

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform VegetationShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    uint shadowLOD;
    float depthBias;
    float slopeBias;
} pc;

// Must match TreeInstanceGPU in VegetationGPUTypes.hpp
struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

// Must match SpeciesRenderInfoGPU in VegetationGPUTypes.hpp
struct SpeciesRenderInfo {
    uint meshletOffset[4];
    uint meshletCount[4];
    uint baseVertexOffset;
    uint materialTextureIndex;
    uint padding[2];
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceCountBuffer {
    uint totalInstanceCount;
};

layout(std430, set = 0, binding = 2) readonly buffer SpeciesRenderInfoBuffer {
    SpeciesRenderInfo speciesInfos[];
};

struct VegetationShadowPayload {
    uint instanceIndex;
    uint baseMeshletIndex;
    uint meshletCount;
};

taskPayloadSharedEXT VegetationShadowPayload payload;

bool sphereInLightFrustum(vec3 center, float radius) {
    mat4 vp = pc.lightViewProjection;
    vec4 planes[6];
    planes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    planes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    planes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    planes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    planes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    planes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    for (int i = 0; i < 6; i++) {
        float len = length(planes[i].xyz);
        if (len < 0.0001) continue;
        planes[i] /= len;
        float d = dot(planes[i].xyz, center) + planes[i].w;
        if (d < -radius) return false;
    }
    return true;
}

void main() {
    uint instIdx = gl_WorkGroupID.x;
    if (instIdx >= pc.instanceCount) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    TreeInstance inst = allInstances[instIdx];

    // Frustum cull against light frustum using bounding sphere
    vec3 center = inst.boundingSphere.xyz;
    float radius = inst.boundingSphere.w;

    if (!sphereInLightFrustum(center, radius)) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    // Select shadow LOD — use the requested shadow LOD or fall back to available
    SpeciesRenderInfo species = speciesInfos[inst.speciesId];
    uint lod = pc.shadowLOD;

    // Find available LOD (try requested, then fall back to lower detail)
    while (lod < 4 && species.meshletCount[lod] == 0) {
        lod++;
    }
    // If no lower LOD available, try higher detail
    if (lod >= 4) {
        lod = pc.shadowLOD;
        while (lod > 0 && species.meshletCount[lod] == 0) {
            lod--;
        }
    }

    uint meshletCount = species.meshletCount[lod];
    if (meshletCount == 0) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    payload.instanceIndex = instIdx;
    payload.baseMeshletIndex = species.meshletOffset[lod];
    payload.meshletCount = meshletCount;

    EmitMeshTasksEXT(meshletCount, 1, 1);
}

#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

// Depth-only mesh shader for vegetation shadows.
// One workgroup per meshlet. Transforms vertices to light space, no color output.

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(push_constant) uniform VegetationShadowPushConstants {
    mat4 lightViewProjection;
    uint instanceCount;
    uint shadowLOD;
    float depthBias;
    float slopeBias;
} pc;

struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
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
    uint instanceIndex;
    uint baseMeshletIndex;
    uint meshletCount;
};

taskPayloadSharedEXT VegetationShadowPayload payload;

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void main() {
    uint meshletLocalIdx = gl_WorkGroupID.x;
    if (meshletLocalIdx >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.baseMeshletIndex + meshletLocalIdx;
    TreeInstance inst = allInstances[payload.instanceIndex];

    GPUMeshlet meshlet = meshlets[globalMeshletIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    mat4 mvp = pc.lightViewProjection * inst.modelMatrix;

    // Transform vertices — depth only, no varyings needed
    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;

            // Vertex stride is 16 floats (64 bytes): pos(3) + normal(3) + uv(2) + bone(8)
            uint baseIdx = globalVertexIndex * 16;

            vec3 position = vec3(
                vertexData[baseIdx + 0],
                vertexData[baseIdx + 1],
                vertexData[baseIdx + 2]
            );

            gl_MeshVerticesEXT[localVertexIndex].gl_Position = mvp * vec4(position, 1.0);
        }
    }

    // Emit primitives
    uint numPrimIterations = (primitiveCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numPrimIterations; iter++) {
        uint localPrimIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localPrimIndex < primitiveCount) {
            uint packedPrimitive = meshletPrimitives[meshlet.primitiveOffset + localPrimIndex];
            uvec3 indices = unpackPrimitive(packedPrimitive);
            gl_PrimitiveTriangleIndicesEXT[localPrimIndex] = indices;
        }
    }
}
