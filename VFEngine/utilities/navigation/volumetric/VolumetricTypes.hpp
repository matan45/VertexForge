#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>

// Volumetric (3D) navigation for flying agents (rewritten for VK-1660): each VolumetricNavVolume entity bakes
// an axis-aligned voxel grid (VoxelGrid) - navigable or blocked per voxel - that the pathfinder searches.
namespace volumetric
{
    struct VoxelCoord
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        bool operator==(const VoxelCoord& other) const = default;
    };

    struct VoxelCoordHash
    {
        size_t operator()(const VoxelCoord& c) const
        {
            size_t h1 = std::hash<int32_t>{}(c.x);
            size_t h2 = std::hash<int32_t>{}(c.y);
            size_t h3 = std::hash<int32_t>{}(c.z);
            return h1 ^ (h2 * 2654435761u) ^ (h3 * 40503u);
        }
    };

    struct VolumetricBakeSettings
    {
        float voxelSize = 1.0f;     // voxel edge length (m)
        float clearance = 0.5f;     // free space kept around blocked geometry (the largest agent radius)
        uint8_t connectivity = 26;  // 6 = face neighbours only, 26 = faces, edges and corners

        bool operator==(const VolumetricBakeSettings&) const = default;
    };

    // The largest grid a volume may bake (16 MB at one byte per voxel).
    inline constexpr int64_t kMaxVoxels = int64_t(1) << 24;
}
