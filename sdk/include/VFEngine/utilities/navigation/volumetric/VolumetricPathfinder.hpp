#pragma once
#include "VolumetricTypes.hpp"
#include "SparseVoxelOctree.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace volumetric
{
    class VolumetricPathfinder
    {
    public:
        VolumePath findPath(const SparseVoxelOctree& grid,
                            const glm::vec3& start, const glm::vec3& end,
                            VoxelConnectivity conn = VoxelConnectivity::TwentySix,
                            int maxNodes = 4096) const;

    private:
        static float heuristic(const VoxelCoord& a, const VoxelCoord& b);

        VolumePath smoothPath(const SparseVoxelOctree& grid,
                              const std::vector<VoxelCoord>& voxelPath) const;

        bool lineOfSight(const SparseVoxelOctree& grid,
                         const VoxelCoord& from, const VoxelCoord& to) const;
    };
}
