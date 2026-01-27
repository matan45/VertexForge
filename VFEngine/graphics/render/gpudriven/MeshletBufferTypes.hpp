#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstddef>

namespace render::gpudriven {

    // Maximum buffer capacities for meshlet rendering
    constexpr uint32_t MAX_MESHLET_COUNT = 4 * 1024 * 1024;         
    constexpr uint32_t MAX_MESHLET_VERTEX_INDICES = 32 * 1024 * 1024; 
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 32 * 1024 * 1024;    

    // Meshlet sizes (must match resource::MAX_MESHLET_VERTICES/PRIMITIVES)
    constexpr uint32_t MESHLET_MAX_VERTICES = 64;
    constexpr uint32_t MESHLET_MAX_PRIMITIVES = 124;

    // GPU-side meshlet structure (must match shader layout)
    //
    // GLSL PACKING NOTE:
    // In GLSL shaders, the vertexCount/primitiveCount/padding0 fields are accessed as a single
    // packed uint called 'vertexPrimCount'. The shader unpacks with:
    //   vertexCount = vertexPrimCount & 0xFF;
    //   primitiveCount = (vertexPrimCount >> 8) & 0xFF;
    // This is memory-compatible with the C++ layout (little-endian):
    //   Byte 0: vertexCount (uint8_t)
    //   Byte 1: primitiveCount (uint8_t)
    //   Bytes 2-3: padding0 (uint16_t)
    // DO NOT reorder these fields without updating the GLSL unpackMeshletCounts() function.
    struct alignas(16) GPUMeshlet {
        uint32_t vertexOffset;
        uint32_t primitiveOffset;
        uint8_t  vertexCount;        // Packed in GLSL as (vertexPrimCount & 0xFF)
        uint8_t  primitiveCount;     // Packed in GLSL as ((vertexPrimCount >> 8) & 0xFF)
        uint16_t padding0;           // Upper 16 bits of packed uint in GLSL (unused)

        uint32_t globalVertexOffset;

        glm::vec4 boundingSphere;

        glm::vec4 cone;
    };
    static_assert(sizeof(GPUMeshlet) == 48, "GPUMeshlet must be 48 bytes");
    static_assert(offsetof(GPUMeshlet, vertexOffset) == 0, "GPUMeshlet::vertexOffset offset mismatch");
    static_assert(offsetof(GPUMeshlet, primitiveOffset) == 4, "GPUMeshlet::primitiveOffset offset mismatch");
    static_assert(offsetof(GPUMeshlet, vertexCount) == 8, "GPUMeshlet::vertexCount offset mismatch - GLSL packing depends on this");
    static_assert(offsetof(GPUMeshlet, primitiveCount) == 9, "GPUMeshlet::primitiveCount offset mismatch - GLSL packing depends on this");
    static_assert(offsetof(GPUMeshlet, globalVertexOffset) == 12, "GPUMeshlet::globalVertexOffset offset mismatch");
    static_assert(offsetof(GPUMeshlet, boundingSphere) == 16, "GPUMeshlet::boundingSphere offset mismatch");
    static_assert(offsetof(GPUMeshlet, cone) == 32, "GPUMeshlet::cone offset mismatch");

    // Per-LOD meshlet info (fits in uvec4 for GPU upload)
    struct alignas(16) MeshletLODInfo {
        uint32_t meshletOffset;      // First meshlet index in global meshlet buffer
        uint32_t meshletCount;
        uint32_t baseVertexOffset;
        uint32_t padding;
    };
    static_assert(sizeof(MeshletLODInfo) == 16, "MeshletLODInfo must be 16 bytes");

}
