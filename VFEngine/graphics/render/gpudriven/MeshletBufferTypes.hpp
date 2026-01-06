#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace render::gpudriven {

    // Maximum buffer capacities for meshlet rendering
    // Default sizes support ~1M meshlets (~80MB total). For larger scenes, pass custom values to init().
    constexpr uint32_t MAX_MESHLET_COUNT = 1 * 1024 * 1024;          // 1M meshlets (48 MB)
    constexpr uint32_t MAX_MESHLET_VERTEX_INDICES = 4 * 1024 * 1024;  // 4M vertex indices (16 MB)
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 4 * 1024 * 1024;      // 4M primitive entries (16 MB)

    // Meshlet sizes (must match resource::MAX_MESHLET_VERTICES/PRIMITIVES)
    constexpr uint32_t MESHLET_MAX_VERTICES = 64;
    constexpr uint32_t MESHLET_MAX_PRIMITIVES = 124;

    // GPU-side meshlet structure (must match shader layout)
    // 48 bytes, 16-byte aligned
    struct alignas(16) GPUMeshlet {
        // Descriptor portion (12 bytes + 4 padding)
        uint32_t vertexOffset;       // Offset into meshlet vertex index buffer
        uint32_t primitiveOffset;    // Offset into meshlet primitive buffer
        uint8_t  vertexCount;        // Number of vertices (0-64)
        uint8_t  primitiveCount;     // Number of triangles (0-124)
        uint16_t padding0;

        // Base vertex offset into merged vertex buffer
        uint32_t globalVertexOffset;

        // Bounding sphere: xyz = center (local space), w = radius
        glm::vec4 boundingSphere;

        // Bounding cone for backface culling
        // xyz = cone axis, w = cos(half-angle), >= 1.0 means no backface culling
        glm::vec4 cone;
    };
    static_assert(sizeof(GPUMeshlet) == 48, "GPUMeshlet must be 48 bytes");

    // Per-LOD meshlet info (fits in uvec4 for GPU upload)
    // 16 bytes, 16-byte aligned
    struct alignas(16) MeshletLODInfo {
        uint32_t meshletOffset;      // First meshlet index in global meshlet buffer
        uint32_t meshletCount;       // Number of meshlets for this LOD
        uint32_t baseVertexOffset;   // Base vertex offset in merged vertex buffer
        uint32_t padding;
    };
    static_assert(sizeof(MeshletLODInfo) == 16, "MeshletLODInfo must be 16 bytes");

    // Meshlet dispatch info for Task/Mesh shader invocation
    // 16 bytes
    struct alignas(16) MeshletDispatchInfo {
        uint32_t objectIndex;        // Index into object buffer
        uint32_t meshletOffset;      // First meshlet for this object
        uint32_t meshletCount;       // Number of meshlets to process
        uint32_t lodLevel;           // Selected LOD level
    };
    static_assert(sizeof(MeshletDispatchInfo) == 16, "MeshletDispatchInfo must be 16 bytes");

    // Statistics for meshlet rendering (matches shader output)
    struct alignas(16) MeshletRenderStats {
        uint32_t totalMeshlets;          // Total meshlets submitted
        uint32_t visibleMeshlets;        // Meshlets passing culling
        uint32_t culledByFrustum;        // Meshlets culled by frustum
        uint32_t culledByBackface;       // Meshlets culled by backface cone
        uint32_t culledByOcclusion;      // Meshlets culled by Hi-Z
        uint32_t trianglesOutput;        // Total triangles emitted
        uint32_t padding[2];
    };
    static_assert(sizeof(MeshletRenderStats) == 32, "MeshletRenderStats must be 32 bytes");

    // Meshlet primitive packing utilities
    // Primitives are stored as 3 uint8 indices per triangle, packed into uint32
    // Format: [idx0 | idx1 | idx2 | padding] in little-endian

    inline uint32_t packMeshletPrimitive(uint8_t idx0, uint8_t idx1, uint8_t idx2) {
        return static_cast<uint32_t>(idx0) |
               (static_cast<uint32_t>(idx1) << 8) |
               (static_cast<uint32_t>(idx2) << 16);
    }

    inline void unpackMeshletPrimitive(uint32_t packed, uint8_t& idx0, uint8_t& idx1, uint8_t& idx2) {
        idx0 = static_cast<uint8_t>(packed & 0xFF);
        idx1 = static_cast<uint8_t>((packed >> 8) & 0xFF);
        idx2 = static_cast<uint8_t>((packed >> 16) & 0xFF);
    }

    // Memory size calculations for buffer allocation
    inline constexpr size_t getMeshletBufferSize(uint32_t meshletCount) {
        return static_cast<size_t>(meshletCount) * sizeof(GPUMeshlet);
    }

    inline constexpr size_t getMeshletVertexBufferSize(uint32_t vertexIndexCount) {
        return static_cast<size_t>(vertexIndexCount) * sizeof(uint32_t);
    }

    inline constexpr size_t getMeshletPrimitiveBufferSize(uint32_t primitiveCount) {
        return static_cast<size_t>(primitiveCount) * sizeof(uint32_t);
    }

}
