#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstddef>

namespace render::gpudriven {

    constexpr uint32_t MAX_MESHLET_COUNT = 4 * 1024 * 1024;
    constexpr uint32_t MAX_MESHLET_VERTEX_INDICES = 32 * 1024 * 1024;
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 32 * 1024 * 1024;

    constexpr uint32_t MESHLET_MAX_VERTICES = 64;
    constexpr uint32_t MESHLET_MAX_PRIMITIVES = 124;

    // GLSL packing: vertexCount/primitiveCount/padding0 are read as a single uint 'vertexPrimCount'.
    // Shader unpacks: vertexCount = vertexPrimCount & 0xFF; primitiveCount = (vertexPrimCount >> 8) & 0xFF;
    // Do not reorder these fields without updating GLSL unpackMeshletCounts().
    struct alignas(16) GPUMeshlet {
        uint32_t vertexOffset;
        uint32_t primitiveOffset;
        uint8_t  vertexCount;
        uint8_t  primitiveCount;
        uint16_t padding0;
        uint32_t globalVertexOffset;
        glm::vec4 boundingSphere;
        glm::vec4 cone;
    };
    static_assert(sizeof(GPUMeshlet) == 48);
    static_assert(offsetof(GPUMeshlet, vertexCount) == 8);
    static_assert(offsetof(GPUMeshlet, primitiveCount) == 9);

    struct alignas(16) MeshletLODInfo {
        uint32_t meshletOffset;
        uint32_t meshletCount;
        uint32_t baseVertexOffset;
        uint32_t padding;
    };
    static_assert(sizeof(MeshletLODInfo) == 16);

}
