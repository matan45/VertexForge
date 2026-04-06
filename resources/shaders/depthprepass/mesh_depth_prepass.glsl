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

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

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

const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

const uint VERTEX_STRIDE = 16; // 64 bytes / 4 bytes per float

// Output world-space normal and roughness to color attachment
layout(location = 0) out vec3 outWorldNormal[];
layout(location = 1) out float outRoughness[];

void main() {
    uint meshletSlot = gl_WorkGroupID.x;
    if (meshletSlot >= payload.meshletCount) return;

    uint globalMeshletIndex = payload.meshletIndices[meshletSlot];
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];

    uint vertexCount = meshlet.vertexPrimCount & 0xFFu;
    uint primitiveCount = (meshlet.vertexPrimCount >> 8) & 0xFFu;

    SetMeshOutputsEXT(vertexCount, primitiveCount);

    PerDrawData drawData = perDrawData[payload.drawIndex];
    mat4 modelMatrix = drawData.modelMatrix;
    mat4 viewProjection = camera.projection * camera.view;
    mat3 normalMatrix = mat3(modelMatrix);
    float roughness = drawData.materialParams.y;

    for (uint i = gl_LocalInvocationID.x; i < vertexCount; i += 32) {
        uint localVertexIndex = meshletVertices[meshlet.vertexOffset + i];
        uint globalVertexIndex = meshlet.globalVertexOffset + localVertexIndex;

        vec3 pos = vec3(
            vertexData[globalVertexIndex * VERTEX_STRIDE + 0],
            vertexData[globalVertexIndex * VERTEX_STRIDE + 1],
            vertexData[globalVertexIndex * VERTEX_STRIDE + 2]
        );

        vec3 normal = vec3(
            vertexData[globalVertexIndex * VERTEX_STRIDE + 3],
            vertexData[globalVertexIndex * VERTEX_STRIDE + 4],
            vertexData[globalVertexIndex * VERTEX_STRIDE + 5]
        );

        vec4 worldPos = modelMatrix * vec4(pos, 1.0);
        gl_MeshVerticesEXT[i].gl_Position = viewProjection * worldPos;
        outWorldNormal[i] = normalize(normalMatrix * normal);
        outRoughness[i] = roughness;
    }

    for (uint i = gl_LocalInvocationID.x; i < primitiveCount; i += 32) {
        uint packed = meshletPrimitives[meshlet.primitiveOffset + i];
        gl_PrimitiveTriangleIndicesEXT[i] = uvec3(
            packed & 0xFFu,
            (packed >> 8) & 0xFFu,
            (packed >> 16) & 0xFFu
        );
    }
}
