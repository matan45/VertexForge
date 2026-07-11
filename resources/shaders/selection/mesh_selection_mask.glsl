#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

// VK-1490 editor selection mask. Position-only clone of the depth-prepass mesh
// shader PLUS the bone-skinning block from gpudriven/mesh_shader_gpudriven.glsl
// (the prepass renders bind pose, which would draw a wrong silhouette for
// animated meshes). The transform chain matches the main pass exactly so the
// LessOrEqual depth test against the resolved scene depth passes on the visible
// surface and fails on occluded fragments — visible-edges-only for free.

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

layout(std430, set = 5, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};

const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
    mat4 instanceModelMatrix;
};

taskPayloadSharedEXT MeshletPayload payload;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

const uint VERTEX_STRIDE = 16; // 64 bytes / 4 bytes per float

void main() {
    uint meshletSlot = gl_WorkGroupID.x;
    if (meshletSlot >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.meshletIndices[meshletSlot];
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];

    uint vertexCount = meshlet.vertexPrimCount & 0xFFu;
    uint primitiveCount = (meshlet.vertexPrimCount >> 8) & 0xFFu;

    SetMeshOutputsEXT(vertexCount, primitiveCount);

    PerDrawData drawData = perDrawData[payload.drawIndex];
    // Instance transform for instanced draws; drawData.modelMatrix otherwise
    // (the task shader packs the right one either way).
    mat4 modelMatrix = payload.instanceModelMatrix;
    mat4 viewProjection = camera.projection * camera.view;

    for (uint i = gl_LocalInvocationID.x; i < vertexCount; i += 32) {
        uint localVertexIndex = meshletVertices[meshlet.vertexOffset + i];
        uint globalVertexIndex = meshlet.globalVertexOffset + localVertexIndex;
        uint baseIdx = globalVertexIndex * VERTEX_STRIDE;

        vec3 position = vec3(
            vertexData[baseIdx + 0],
            vertexData[baseIdx + 1],
            vertexData[baseIdx + 2]
        );

        // Skinning — identical to mesh_shader_gpudriven.glsl so the animated
        // silhouette matches the scene pass.
        if (drawData.boneMatrixOffset != 0xFFFFFFFFu) {
            ivec4 boneIndices = ivec4(
                floatBitsToInt(vertexData[baseIdx + 8]),
                floatBitsToInt(vertexData[baseIdx + 9]),
                floatBitsToInt(vertexData[baseIdx + 10]),
                floatBitsToInt(vertexData[baseIdx + 11])
            );
            vec4 boneWeights = vec4(
                vertexData[baseIdx + 12],
                vertexData[baseIdx + 13],
                vertexData[baseIdx + 14],
                vertexData[baseIdx + 15]
            );

            mat4 skinMatrix = mat4(0.0);
            float totalWeight = 0.0;
            for (int b = 0; b < 4; ++b) {
                int boneIdx = boneIndices[b];
                float weight = boneWeights[b];
                if (boneIdx >= 0 && weight > 0.0) {
                    uint globalBoneIdx = drawData.boneMatrixOffset + uint(boneIdx);
                    skinMatrix += boneMatrices[globalBoneIdx] * weight;
                    totalWeight += weight;
                }
            }

            if (totalWeight > 0.0) {
                position = (skinMatrix * vec4(position, 1.0)).xyz;
            }
        }

        vec4 worldPos = modelMatrix * vec4(position, 1.0);
        gl_MeshVerticesEXT[i].gl_Position = viewProjection * worldPos;
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
