#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include "Types.hpp"

namespace resource
{
    // Standard meshlet sizes based on NVIDIA recommendations
    // These must be <= 255 as they're stored in uint8 fields and packed indices
    constexpr uint32_t MAX_MESHLET_VERTICES = 64;
    constexpr uint32_t MAX_MESHLET_PRIMITIVES = 124; // 126 max, use 124 for alignment

    // Compile-time validation: indices are packed as uint8, counts stored in uint8 fields
    static_assert(MAX_MESHLET_VERTICES <= 255,
        "MAX_MESHLET_VERTICES must be <= 255 (triangle indices packed as uint8)");
    static_assert(MAX_MESHLET_PRIMITIVES <= 255,
        "MAX_MESHLET_PRIMITIVES must be <= 255 (primitive count stored as uint8)");

    // Meshlet descriptor - describes one meshlet's data location
    struct MeshletDescriptor
    {
        uint32_t vertexOffset;
        uint32_t primitiveOffset;
        uint8_t vertexCount;
        uint8_t primitiveCount;
        uint16_t padding;
    };

    static_assert(sizeof(MeshletDescriptor) == 12, "MeshletDescriptor must be 12 bytes");

    // Bounding volume for per-meshlet culling (32 bytes)
    struct MeshletBounds
    {
        // Bounding sphere: xyz = center (local space), w = radius
        glm::vec4 boundingSphere;

        // Bounding cone for backface culling (normal cone)
        // xyz = cone axis (normalized direction), w = cos(cone half-angle)
        // If cone.w >= 1.0, backface culling is disabled for this meshlet
        glm::vec4 cone;
    };

    static_assert(sizeof(MeshletBounds) == 32, "MeshletBounds must be 32 bytes");

    // Combined meshlet data for CPU storage (44 bytes)
    struct Meshlet
    {
        MeshletDescriptor descriptor;
        MeshletBounds bounds;
    };

    static_assert(sizeof(Meshlet) == 44, "Meshlet must be 44 bytes");

    // Per-LOD meshlet info for a submesh
    struct LODMeshletInfo
    {
        uint32_t meshletOffset;
        uint32_t meshletCount;
        uint32_t vertexDataOffset;
        uint32_t vertexDataCount;
        uint32_t primitiveDataOffset;
        uint32_t primitiveDataCount;
    };

    // Submesh meshlet data (CPU-side storage)
    struct SubmeshMeshletData
    {
        std::string name;
        std::array<LODMeshletInfo, LOD_LEVEL_COUNT> lodLevels{};

        std::vector<Meshlet> meshlets;

        std::vector<uint32_t> meshletVertices;

        // Primitive indices (packed as uint32: 3 uint8_t indices + padding)
        // Each triangle uses indices [0-63] into the meshlet's local vertex array
        std::vector<uint32_t> meshletPrimitives;

        // Check if meshlet data is available
        [[nodiscard]] bool hasMeshletData() const
        {
            return !meshlets.empty();
        }
    };
}
