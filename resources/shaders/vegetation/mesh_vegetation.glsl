#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragInstanceIndex[];
layout(location = 4) flat out uint fragMaterialTexIndex[];
layout(location = 5) flat out uint fragLodLevel[];

// Must match TreeInstanceGPU in VegetationGPUTypes.hpp
struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

layout(std430, set = 0, binding = 2) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
};

layout(set = 1, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Meshlet data (shared with main mesh pipeline)
layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 3, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 3, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 4, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

struct VegetationPayload {
    uint instanceIndex;
    uint baseMeshletIndex;
    uint meshletCount;
    uint baseVertexOffset;
    uint materialTextureIndex;
    uint lodLevel;
};

taskPayloadSharedEXT VegetationPayload payload;

shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];
shared vec2 sharedTexCoords[MESHLET_MAX_VERTICES];

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
    uint instanceIdx = payload.instanceIndex;
    TreeInstance inst = allInstances[instanceIdx];

    GPUMeshlet meshlet = meshlets[globalMeshletIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    mat4 modelMatrix = inst.modelMatrix;
    mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    mat4 viewProjection = camera.projection * camera.view;

    // Load vertices
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
            vec3 normal = vec3(
                vertexData[baseIdx + 3],
                vertexData[baseIdx + 4],
                vertexData[baseIdx + 5]
            );

            sharedPositions[localVertexIndex] = position;
            sharedNormals[localVertexIndex] = normal;
            sharedTexCoords[localVertexIndex] = vec2(
                vertexData[baseIdx + 6],
                vertexData[baseIdx + 7]
            );
        }
    }

    barrier();

    // Transform and emit vertices
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec3 localPos = sharedPositions[localVertexIndex];
            vec4 worldPos4 = modelMatrix * vec4(localPos, 1.0);
            vec3 worldPos = worldPos4.xyz;

            fragWorldPos[localVertexIndex] = worldPos;
            fragNormal[localVertexIndex] = normalize(normalMatrix * sharedNormals[localVertexIndex]);
            fragTexCoord[localVertexIndex] = sharedTexCoords[localVertexIndex];
            fragInstanceIndex[localVertexIndex] = instanceIdx;
            fragMaterialTexIndex[localVertexIndex] = payload.materialTextureIndex;
            fragLodLevel[localVertexIndex] = payload.lodLevel;
            gl_MeshVerticesEXT[localVertexIndex].gl_Position = viewProjection * vec4(worldPos, 1.0);
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
