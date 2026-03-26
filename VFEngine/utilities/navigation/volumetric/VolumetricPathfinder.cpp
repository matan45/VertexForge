#include "VolumetricPathfinder.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

namespace volumetric
{
    namespace
    {
        struct AStarNode
        {
            VoxelCoord coord;
            float gCost = 0.0f;
            float fCost = 0.0f;

            bool operator>(const AStarNode& other) const
            {
                return fCost > other.fCost;
            }
        };

        float edgeCost(int dx, int dy, int dz)
        {
            int absDx = std::abs(dx);
            int absDy = std::abs(dy);
            int absDz = std::abs(dz);
            int nonZero = absDx + absDy + absDz;

            if (nonZero == 1)
            {
                return 1.0f;
            }
            if (nonZero == 2)
            {
                return 1.41421356f; // sqrt(2)
            }
            return 1.73205081f; // sqrt(3)
        }
    }

    float VolumetricPathfinder::heuristic(const VoxelCoord& a, const VoxelCoord& b)
    {
        // Octile distance in 3D
        int dx = std::abs(a.x - b.x);
        int dy = std::abs(a.y - b.y);
        int dz = std::abs(a.z - b.z);

        // Sort so that d1 <= d2 <= d3
        int vals[3] = {dx, dy, dz};
        if (vals[0] > vals[1]) std::swap(vals[0], vals[1]);
        if (vals[1] > vals[2]) std::swap(vals[1], vals[2]);
        if (vals[0] > vals[1]) std::swap(vals[0], vals[1]);

        int d1 = vals[0]; // smallest
        int d2 = vals[1];
        int d3 = vals[2]; // largest

        // space diagonals + face diagonals + axis-aligned
        return (1.73205081f - 1.41421356f) * static_cast<float>(d1)
             + (1.41421356f - 1.0f) * static_cast<float>(d2)
             + 1.0f * static_cast<float>(d3);
    }

    VolumePath VolumetricPathfinder::findPath(const SparseVoxelOctree& grid,
                                               const glm::vec3& start, const glm::vec3& end,
                                               VoxelConnectivity conn, int maxNodes) const
    {
        VolumePath result;

        VoxelCoord startCoord = grid.worldToVoxel(start);
        VoxelCoord endCoord = grid.worldToVoxel(end);

        if (!grid.isNavigable(startCoord) || !grid.isNavigable(endCoord))
        {
            return result;
        }

        if (startCoord == endCoord)
        {
            result.waypoints.push_back(start);
            result.waypoints.push_back(end);
            result.isValid = true;
            return result;
        }

        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openList;
        std::unordered_map<VoxelCoord, float, VoxelCoordHash> gCosts;
        std::unordered_map<VoxelCoord, VoxelCoord, VoxelCoordHash> cameFrom;
        std::unordered_set<VoxelCoord, VoxelCoordHash> closedSet;

        float startH = heuristic(startCoord, endCoord);
        openList.push(AStarNode{startCoord, 0.0f, startH});
        gCosts[startCoord] = 0.0f;

        int nodesExpanded = 0;
        VoxelCoord bestNode = startCoord;
        float bestH = startH;

        while (!openList.empty() && nodesExpanded < maxNodes)
        {
            AStarNode current = openList.top();
            openList.pop();

            if (current.coord == endCoord)
            {
                // Reconstruct path
                std::vector<VoxelCoord> voxelPath;
                VoxelCoord c = endCoord;
                while (!(c == startCoord))
                {
                    voxelPath.push_back(c);
                    c = cameFrom[c];
                }
                voxelPath.push_back(startCoord);
                std::reverse(voxelPath.begin(), voxelPath.end());

                result = smoothPath(grid, voxelPath);
                result.isValid = true;
                result.isPartial = false;

                // Replace first and last waypoints with exact world positions
                if (!result.waypoints.empty())
                {
                    result.waypoints.front() = start;
                    result.waypoints.back() = end;
                }

                return result;
            }

            if (closedSet.count(current.coord))
            {
                continue;
            }
            closedSet.insert(current.coord);
            ++nodesExpanded;

            // Track best node for partial path
            float currentH = heuristic(current.coord, endCoord);
            if (currentH < bestH)
            {
                bestH = currentH;
                bestNode = current.coord;
            }

            auto neighbors = grid.getNavigableNeighbors(current.coord, conn);
            for (const auto& neighbor : neighbors)
            {
                if (closedSet.count(neighbor))
                {
                    continue;
                }

                int dx = neighbor.x - current.coord.x;
                int dy = neighbor.y - current.coord.y;
                int dz = neighbor.z - current.coord.z;
                float tentativeG = current.gCost + edgeCost(dx, dy, dz);

                auto it = gCosts.find(neighbor);
                if (it != gCosts.end() && tentativeG >= it->second)
                {
                    continue;
                }

                gCosts[neighbor] = tentativeG;
                cameFrom[neighbor] = current.coord;
                float h = heuristic(neighbor, endCoord);
                openList.push(AStarNode{neighbor, tentativeG, tentativeG + h});
            }
        }

        // Return partial path to best node
        if (bestNode != startCoord)
        {
            std::vector<VoxelCoord> voxelPath;
            VoxelCoord c = bestNode;
            while (!(c == startCoord))
            {
                voxelPath.push_back(c);
                c = cameFrom[c];
            }
            voxelPath.push_back(startCoord);
            std::reverse(voxelPath.begin(), voxelPath.end());

            result = smoothPath(grid, voxelPath);
            result.isValid = true;
            result.isPartial = true;

            if (!result.waypoints.empty())
            {
                result.waypoints.front() = start;
            }
        }

        return result;
    }

    bool VolumetricPathfinder::lineOfSight(const SparseVoxelOctree& grid,
                                            const VoxelCoord& from, const VoxelCoord& to) const
    {
        // 3D Bresenham line algorithm
        int dx = std::abs(to.x - from.x);
        int dy = std::abs(to.y - from.y);
        int dz = std::abs(to.z - from.z);

        int sx = (to.x > from.x) ? 1 : -1;
        int sy = (to.y > from.y) ? 1 : -1;
        int sz = (to.z > from.z) ? 1 : -1;

        int maxDim = std::max({dx, dy, dz});
        if (maxDim == 0)
        {
            return true;
        }

        int x = from.x;
        int y = from.y;
        int z = from.z;

        // Use the dominant axis to drive the iteration
        if (dx >= dy && dx >= dz)
        {
            int errY = 2 * dy - dx;
            int errZ = 2 * dz - dx;

            for (int i = 0; i <= dx; ++i)
            {
                if (!grid.isNavigable(VoxelCoord{x, y, z}))
                {
                    return false;
                }
                if (errY > 0)
                {
                    y += sy;
                    errY -= 2 * dx;
                }
                if (errZ > 0)
                {
                    z += sz;
                    errZ -= 2 * dx;
                }
                errY += 2 * dy;
                errZ += 2 * dz;
                x += sx;
            }
        }
        else if (dy >= dx && dy >= dz)
        {
            int errX = 2 * dx - dy;
            int errZ = 2 * dz - dy;

            for (int i = 0; i <= dy; ++i)
            {
                if (!grid.isNavigable(VoxelCoord{x, y, z}))
                {
                    return false;
                }
                if (errX > 0)
                {
                    x += sx;
                    errX -= 2 * dy;
                }
                if (errZ > 0)
                {
                    z += sz;
                    errZ -= 2 * dy;
                }
                errX += 2 * dx;
                errZ += 2 * dz;
                y += sy;
            }
        }
        else
        {
            int errX = 2 * dx - dz;
            int errY = 2 * dy - dz;

            for (int i = 0; i <= dz; ++i)
            {
                if (!grid.isNavigable(VoxelCoord{x, y, z}))
                {
                    return false;
                }
                if (errX > 0)
                {
                    x += sx;
                    errX -= 2 * dz;
                }
                if (errY > 0)
                {
                    y += sy;
                    errY -= 2 * dz;
                }
                errX += 2 * dx;
                errY += 2 * dy;
                z += sz;
            }
        }

        return true;
    }

    VolumePath VolumetricPathfinder::smoothPath(const SparseVoxelOctree& grid,
                                                 const std::vector<VoxelCoord>& voxelPath) const
    {
        VolumePath result;

        if (voxelPath.empty())
        {
            return result;
        }

        if (voxelPath.size() == 1)
        {
            result.waypoints.push_back(grid.voxelToWorld(voxelPath[0]));
            return result;
        }

        // Greedy line-of-sight smoothing
        std::vector<VoxelCoord> smoothed;
        smoothed.push_back(voxelPath[0]);

        size_t current = 0;
        while (current < voxelPath.size() - 1)
        {
            size_t furthest = current + 1;
            for (size_t test = voxelPath.size() - 1; test > current + 1; --test)
            {
                if (lineOfSight(grid, voxelPath[current], voxelPath[test]))
                {
                    furthest = test;
                    break;
                }
            }
            smoothed.push_back(voxelPath[furthest]);
            current = furthest;
        }

        result.waypoints.reserve(smoothed.size());
        for (const auto& vc : smoothed)
        {
            result.waypoints.push_back(grid.voxelToWorld(vc));
        }

        return result;
    }
}
