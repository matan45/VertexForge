#include "SparseVoxelOctree.hpp"
#include <cmath>
#include <algorithm>

namespace volumetric
{
    void SparseVoxelOctree::build(const std::vector<uint8_t>& denseGrid, glm::ivec3 dimensions,
                                   glm::vec3 worldOrigin, float cellSize)
    {
        dims = dimensions;
        origin = worldOrigin;
        voxelSize = cellSize;

        size_t totalSize = static_cast<size_t>(dims.x) * dims.y * dims.z;
        grid.resize(totalSize, 0);

        size_t copySize = std::min(denseGrid.size(), totalSize);
        for (size_t i = 0; i < copySize; ++i)
        {
            grid[i] = denseGrid[i] != 0 ? 1 : 0;
        }
    }

    bool SparseVoxelOctree::isNavigable(VoxelCoord coord) const
    {
        if (!isInBounds(coord))
        {
            return false;
        }
        return grid[index(coord.x, coord.y, coord.z)] != 0;
    }

    std::vector<VoxelCoord> SparseVoxelOctree::getNavigableNeighbors(VoxelCoord coord,
                                                                       VoxelConnectivity conn) const
    {
        std::vector<VoxelCoord> neighbors;

        if (conn == VoxelConnectivity::Six)
        {
            neighbors.reserve(6);
            const int offsets[6][3] = {
                {1, 0, 0}, {-1, 0, 0},
                {0, 1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1}
            };

            for (const auto& off : offsets)
            {
                VoxelCoord n{coord.x + off[0], coord.y + off[1], coord.z + off[2]};
                if (isNavigable(n))
                {
                    neighbors.push_back(n);
                }
            }
        }
        else
        {
            neighbors.reserve(26);
            for (int dx = -1; dx <= 1; ++dx)
            {
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        if (dx == 0 && dy == 0 && dz == 0)
                        {
                            continue;
                        }
                        VoxelCoord n{coord.x + dx, coord.y + dy, coord.z + dz};
                        if (isNavigable(n))
                        {
                            neighbors.push_back(n);
                        }
                    }
                }
            }
        }

        return neighbors;
    }

    void SparseVoxelOctree::markBlocked(VoxelCoord coord)
    {
        if (isInBounds(coord))
        {
            grid[index(coord.x, coord.y, coord.z)] = 0;
        }
    }

    void SparseVoxelOctree::markNavigable(VoxelCoord coord)
    {
        if (isInBounds(coord))
        {
            grid[index(coord.x, coord.y, coord.z)] = 1;
        }
    }

    VoxelCoord SparseVoxelOctree::worldToVoxel(glm::vec3 worldPos) const
    {
        glm::vec3 local = (worldPos - origin) / voxelSize;
        return VoxelCoord{
            static_cast<int32_t>(std::floor(local.x)),
            static_cast<int32_t>(std::floor(local.y)),
            static_cast<int32_t>(std::floor(local.z))
        };
    }

    glm::vec3 SparseVoxelOctree::voxelToWorld(VoxelCoord coord) const
    {
        return origin + glm::vec3(
            (static_cast<float>(coord.x) + 0.5f) * voxelSize,
            (static_cast<float>(coord.y) + 0.5f) * voxelSize,
            (static_cast<float>(coord.z) + 0.5f) * voxelSize
        );
    }

    bool SparseVoxelOctree::isInBounds(VoxelCoord coord) const
    {
        return coord.x >= 0 && coord.x < dims.x
            && coord.y >= 0 && coord.y < dims.y
            && coord.z >= 0 && coord.z < dims.z;
    }

    void SparseVoxelOctree::clear()
    {
        grid.clear();
        dims = glm::ivec3(0);
        origin = glm::vec3(0.0f);
        voxelSize = 1.0f;
    }

    size_t SparseVoxelOctree::getNodeCount() const
    {
        return grid.size();
    }

    size_t SparseVoxelOctree::getNavigableCount() const
    {
        size_t count = 0;
        for (uint8_t v : grid)
        {
            if (v != 0)
            {
                ++count;
            }
        }
        return count;
    }
}
