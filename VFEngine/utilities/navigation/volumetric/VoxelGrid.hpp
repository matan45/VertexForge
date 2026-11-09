#pragma once
#include "VolumetricTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace volumetric
{
    // One baked volume (VK-1660; replaces the flat "SparseVoxelOctree"): an axis-aligned grid of voxels, each
    // navigable or blocked, one byte per voxel, int64 indexing. Immutable once shared (the pathfinder reads it
    // from several threads); build or decode it, then hand it out as shared_ptr<const VoxelGrid>.
    class VoxelGrid
    {
    public:
        VoxelGrid() = default;
        // Every voxel starts blocked. dims must be positive with dimX * dimY * dimZ <= kMaxVoxels.
        VoxelGrid(const glm::vec3& origin, const glm::ivec3& dims, const VolumetricBakeSettings& settings);

        const glm::vec3& origin() const { return originPoint; } // world position of voxel (0,0,0)'s min corner
        const glm::ivec3& dims() const { return dimensions; }
        const VolumetricBakeSettings& settings() const { return bakeSettings; }
        float voxelSize() const { return bakeSettings.voxelSize; }
        int64_t voxelCount() const { return static_cast<int64_t>(cells.size()); }
        bool empty() const { return cells.empty(); }

        glm::vec3 worldMin() const { return originPoint; }
        glm::vec3 worldMax() const;

        bool inBounds(const VoxelCoord& c) const;
        int64_t index(const VoxelCoord& c) const; // x + dimX * (y + dimY * z); c must be inBounds
        VoxelCoord coordOf(int64_t index) const;
        glm::vec3 centerOf(const VoxelCoord& c) const;
        // The voxel containing `world`; nullopt outside the grid (the max faces are outside).
        std::optional<VoxelCoord> coordAt(const glm::vec3& world) const;

        bool isNavigable(const VoxelCoord& c) const; // false outside the grid
        void setNavigable(const VoxelCoord& c, bool navigable);
        int64_t navigableCount() const;               // counted on each call

        // The navigable neighbours of `c` under the grid's connectivity (6 or 26). A diagonal step is left out
        // when a voxel it cuts past is blocked: an edge step needs both face voxels it passes free, a corner step
        // the three face and three edge voxels - so a path never squeezes through a crack.
        void neighbors(const VoxelCoord& c, std::vector<VoxelCoord>& out) const;

        // Raw cells (1 = navigable, 0 = blocked) in index order: the voxelizer and the serializer.
        const std::vector<uint8_t>& data() const { return cells; }
        std::vector<uint8_t>& data() { return cells; }

    private:
        glm::vec3 originPoint{0.0f};
        glm::ivec3 dimensions{0};
        VolumetricBakeSettings bakeSettings;
        std::vector<uint8_t> cells;
    };
}
