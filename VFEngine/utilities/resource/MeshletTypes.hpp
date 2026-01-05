#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include "Types.hpp"

namespace resource {

    // Standard meshlet sizes based on NVIDIA recommendations
    // Validated against device capabilities at runtime
    constexpr uint32_t MAX_MESHLET_VERTICES = 64;
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 124;  // 126 max, use 124 for alignment
    constexpr uint32_t MAX_MESHLET_INDICES = MAX_MESHLET_PRIMITIVES * 3;

    // Meshlet descriptor - describes one meshlet's data location
    // 12 bytes
    struct MeshletDescriptor {
        uint32_t vertexOffset;      // Offset into meshlet vertex buffer
        uint32_t primitiveOffset;   // Offset into meshlet primitive buffer
        uint8_t  vertexCount;       // Number of unique vertices (max 64)
        uint8_t  primitiveCount;    // Number of triangles (max 124)
        uint16_t padding;           // Alignment padding
    };
    static_assert(sizeof(MeshletDescriptor) == 12, "MeshletDescriptor must be 12 bytes");

    // Bounding volume for per-meshlet culling (32 bytes)
    struct MeshletBounds {
        // Bounding sphere: xyz = center (local space), w = radius
        glm::vec4 boundingSphere;

        // Bounding cone for backface culling (normal cone)
        // xyz = cone axis (normalized direction), w = cos(cone half-angle)
        // If cone.w >= 1.0, backface culling is disabled for this meshlet
        glm::vec4 cone;
    };
    static_assert(sizeof(MeshletBounds) == 32, "MeshletBounds must be 32 bytes");

    // Combined meshlet data for CPU storage (44 bytes)
    struct Meshlet {
        MeshletDescriptor descriptor;
        MeshletBounds bounds;
    };
    static_assert(sizeof(Meshlet) == 44, "Meshlet must be 44 bytes");

    // Per-LOD meshlet info for a submesh
    struct LODMeshletInfo {
        uint32_t meshletOffset;        // First meshlet index for this LOD
        uint32_t meshletCount;         // Number of meshlets in this LOD
        uint32_t vertexDataOffset;     // Offset into meshlet vertex buffer
        uint32_t vertexDataCount;      // Number of vertex indices for this LOD
        uint32_t primitiveDataOffset;  // Offset into meshlet primitive buffer
        uint32_t primitiveDataCount;   // Number of primitive indices for this LOD
    };

    // Submesh meshlet data (CPU-side storage)
    struct SubmeshMeshletData {
        std::string name;
        std::array<LODMeshletInfo, LOD_LEVEL_COUNT> lodLevels{};

        // All meshlets for this submesh (all LODs concatenated)
        std::vector<Meshlet> meshlets;

        // Local vertex indices -> global vertex indices
        // Each meshlet's vertexOffset indexes into this
        std::vector<uint32_t> meshletVertices;

        // Primitive indices (packed as uint32: 3 uint8_t indices + padding)
        // Each triangle uses indices [0-63] into the meshlet's local vertex array
        std::vector<uint32_t> meshletPrimitives;

        // Get total meshlet count across all LODs
        [[nodiscard]] uint32_t getTotalMeshletCount() const {
            return static_cast<uint32_t>(meshlets.size());
        }

        // Check if meshlet data is available
        [[nodiscard]] bool hasMeshletData() const {
            return !meshlets.empty();
        }
    };

    // Complete mesh meshlet data (for file format)
    struct MeshMeshletData {
        std::vector<SubmeshMeshletData> submeshes;

        [[nodiscard]] bool hasMeshletData() const {
            for (const auto& submesh : submeshes) {
                if (submesh.hasMeshletData()) return true;
            }
            return false;
        }
    };

    // Extended MeshData with meshlet support
    struct MeshDataWithMeshlets {
        std::string name;
        std::vector<LODLevel> lodLevels;
        SubmeshMeshletData meshletData;

        // Access LOD0 vertices/indices for backward compatibility
        [[nodiscard]] const std::vector<Vertex>& vertices() const {
            static std::vector<Vertex> empty;
            return lodLevels.empty() ? empty : lodLevels[0].vertices;
        }

        [[nodiscard]] const std::vector<uint32_t>& indices() const {
            static std::vector<uint32_t> empty;
            return lodLevels.empty() ? empty : lodLevels[0].indices;
        }
    };

    // Extended MeshesData with meshlet support
    struct MeshesDataWithMeshlets {
        FileType headerFileType = FileType::MESH;
        FileVersion version{};
        uint32_t numberOfMeshes = 0;
        std::vector<MeshDataWithMeshlets> meshes;

        [[nodiscard]] bool hasMeshletData() const {
            for (const auto& mesh : meshes) {
                if (mesh.meshletData.hasMeshletData()) return true;
            }
            return false;
        }
    };

}
