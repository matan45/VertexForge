#pragma once
#include "VolumetricTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace volumetric
{
    class SparseVoxelOctree
    {
    public:
        void build(const std::vector<uint8_t>& denseGrid, glm::ivec3 dimensions,
                    glm::vec3 worldOrigin, float cellSize);

        bool isNavigable(VoxelCoord coord) const;
        std::vector<VoxelCoord> getNavigableNeighbors(VoxelCoord coord, VoxelConnectivity conn) const;

        void markBlocked(VoxelCoord coord);
        void markNavigable(VoxelCoord coord);

        VoxelCoord worldToVoxel(glm::vec3 worldPos) const;
        glm::vec3 voxelToWorld(VoxelCoord coord) const;
        bool isInBounds(VoxelCoord coord) const;

        void clear();

        size_t getNodeCount() const;
        size_t getNavigableCount() const;

        const glm::ivec3& getDims() const { return dims; }
        const glm::vec3& getOrigin() const { return origin; }
        float getVoxelSize() const { return voxelSize; }
        const std::vector<uint8_t>& getGrid() const { return grid; }

    private:
        std::vector<uint8_t> grid;  // 0=blocked, 1=navigable
        glm::ivec3 dims{0};
        glm::vec3 origin{0.0f};
        float voxelSize = 1.0f;

        int index(int x, int y, int z) const
        {
            return x + y * dims.x + z * dims.x * dims.y;
        }
    };
}
