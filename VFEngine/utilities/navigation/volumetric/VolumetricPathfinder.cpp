#include "VolumetricPathfinder.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace volumetric
{
    namespace
    {
        float coordDistance(const VoxelCoord& a, const VoxelCoord& b)
        {
            const float dx = static_cast<float>(a.x - b.x);
            const float dy = static_cast<float>(a.y - b.y);
            const float dz = static_cast<float>(a.z - b.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        struct OpenEntry
        {
            float f = 0.0f;
            float g = 0.0f;
            int64_t index = 0;

            bool operator>(const OpenEntry& other) const
            {
                if (f != other.f)
                    return f > other.f;
                return index > other.index; // deterministic order on ties
            }
        };

        struct NodeRecord
        {
            float g = std::numeric_limits<float>::infinity();
            int64_t parent = -1;
            bool closed = false;
        };

        // Drops every waypoint whose neighbours see each other through navigable voxels (greedy string pulling).
        std::vector<glm::vec3> smoothPath(const VoxelGrid& grid, const std::vector<glm::vec3>& points)
        {
            if (points.size() <= 2)
                return points;

            std::vector<glm::vec3> smoothed;
            smoothed.push_back(points.front());
            size_t anchor = 0;
            for (size_t i = 2; i < points.size(); ++i)
            {
                if (!hasLineOfSight(grid, points[anchor], points[i]))
                {
                    smoothed.push_back(points[i - 1]);
                    anchor = i - 1;
                }
            }
            smoothed.push_back(points.back());
            return smoothed;
        }
    }

    const char* volumePathStatusName(VolumePathStatus status)
    {
        switch (status)
        {
        case VolumePathStatus::Found: return "Found";
        case VolumePathStatus::Partial: return "Partial";
        case VolumePathStatus::NoVolume: return "NoVolume";
        case VolumePathStatus::OutsideVolume: return "OutsideVolume";
        case VolumePathStatus::StartBlocked: return "StartBlocked";
        case VolumePathStatus::EndBlocked: return "EndBlocked";
        case VolumePathStatus::DifferentVolumes: return "DifferentVolumes";
        case VolumePathStatus::NoPath: return "NoPath";
        }
        return "Unknown";
    }

    std::optional<glm::vec3> snapToNavigable(const VoxelGrid& grid, const glm::vec3& point, float snapRadius)
    {
        const std::optional<VoxelCoord> origin = grid.coordAt(point);
        if (!origin)
            return std::nullopt;
        if (grid.isNavigable(*origin))
            return point;
        if (!std::isfinite(snapRadius) || !(snapRadius > 0.0f))
            return std::nullopt;

        const float voxelSize = grid.voxelSize();
        const int maxShell = static_cast<int>(std::ceil(static_cast<double>(snapRadius) / static_cast<double>(voxelSize)));
        float bestDistanceSquared = snapRadius * snapRadius;
        std::optional<glm::vec3> best;

        for (int shell = 1; shell <= maxShell; ++shell)
        {
            // A voxel on Chebyshev shell k has its center at least (k - 0.5) voxels from any point of the
            // origin voxel, so once that bound passes the best distance no later shell can do better.
            const float shellBound = (static_cast<float>(shell) - 0.5f) * voxelSize;
            if (best && shellBound * shellBound > bestDistanceSquared)
                break;

            for (int dz = -shell; dz <= shell; ++dz)
                for (int dy = -shell; dy <= shell; ++dy)
                    for (int dx = -shell; dx <= shell; ++dx)
                    {
                        if (std::max({std::abs(dx), std::abs(dy), std::abs(dz)}) != shell)
                            continue; // inside the shell: an earlier one
                        const VoxelCoord candidate{origin->x + dx, origin->y + dy, origin->z + dz};
                        if (!grid.isNavigable(candidate))
                            continue;
                        const glm::vec3 center = grid.centerOf(candidate);
                        const glm::vec3 delta = center - point;
                        const float distanceSquared = glm::dot(delta, delta);
                        if (distanceSquared <= bestDistanceSquared)
                        {
                            bestDistanceSquared = distanceSquared;
                            best = center;
                        }
                    }
        }
        return best;
    }

    bool hasLineOfSight(const VoxelGrid& grid, const glm::vec3& from, const glm::vec3& to)
    {
        const std::optional<VoxelCoord> startCoord = grid.coordAt(from);
        const std::optional<VoxelCoord> endCoord = grid.coordAt(to);
        if (!startCoord || !endCoord)
            return false;
        if (!grid.isNavigable(*startCoord) || !grid.isNavigable(*endCoord))
            return false;

        // Amanatides-Woo traversal. Steps that cross several voxel faces at once (an edge or a corner) also need
        // the voxels on either side free - the same no-corner-cutting rule as VoxelGrid::neighbors.
        const glm::vec3 direction = to - from;
        const float voxelSize = grid.voxelSize();
        const glm::vec3 local = (from - grid.origin()) / voxelSize;

        VoxelCoord current = *startCoord;
        int step[3] = {0, 0, 0};
        float tMax[3] = {0.0f, 0.0f, 0.0f};
        float tDelta[3] = {0.0f, 0.0f, 0.0f};
        const int coord[3] = {current.x, current.y, current.z};
        for (int axis = 0; axis < 3; ++axis)
        {
            const float d = direction[axis] / voxelSize; // voxels per unit t
            if (d > 0.0f)
            {
                step[axis] = 1;
                tDelta[axis] = 1.0f / d;
                tMax[axis] = (static_cast<float>(coord[axis] + 1) - local[axis]) / d;
            }
            else if (d < 0.0f)
            {
                step[axis] = -1;
                tDelta[axis] = -1.0f / d;
                tMax[axis] = (local[axis] - static_cast<float>(coord[axis])) / -d;
            }
            else
            {
                tMax[axis] = std::numeric_limits<float>::infinity();
                tDelta[axis] = std::numeric_limits<float>::infinity();
            }
        }

        const int span = std::abs(endCoord->x - startCoord->x) + std::abs(endCoord->y - startCoord->y) +
                         std::abs(endCoord->z - startCoord->z);
        constexpr float kTieTolerance = 1.0e-6f;
        for (int guard = 0; current != *endCoord; ++guard)
        {
            if (guard > span + 3)
                return false; // the walk does not converge (numerical trouble): refuse rather than guess

            const float nearest = std::min({tMax[0], tMax[1], tMax[2]});
            if (!std::isfinite(nearest) || nearest > 1.0f + kTieTolerance)
                break; // past the end point: the end voxel is reached through rounding only

            bool moves[3] = {false, false, false};
            int moving = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                if (tMax[axis] <= nearest + kTieTolerance)
                {
                    moves[axis] = true;
                    ++moving;
                }
            }

            // Several faces at once: every partial step must be free as well.
            if (moving > 1)
            {
                for (int mask = 1; mask < 7; ++mask)
                {
                    int count = 0;
                    VoxelCoord partial = current;
                    bool usable = true;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if ((mask & (1 << axis)) == 0)
                            continue;
                        if (!moves[axis])
                        {
                            usable = false;
                            break;
                        }
                        ++count;
                        if (axis == 0) partial.x += step[axis];
                        else if (axis == 1) partial.y += step[axis];
                        else partial.z += step[axis];
                    }
                    if (!usable || count == moving)
                        continue;
                    if (!grid.isNavigable(partial))
                        return false;
                }
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                if (!moves[axis])
                    continue;
                if (axis == 0) current.x += step[axis];
                else if (axis == 1) current.y += step[axis];
                else current.z += step[axis];
                tMax[axis] += tDelta[axis];
            }
            if (!grid.isNavigable(current))
                return false;
        }
        return true;
    }

    VolumePathResult findPath(const VoxelGrid& grid, const PathRequest& request)
    {
        VolumePathResult result;
        std::optional<VoxelCoord> startCoord = grid.coordAt(request.start);
        std::optional<VoxelCoord> endCoord = grid.coordAt(request.end);
        if (!startCoord || !endCoord)
        {
            result.status = VolumePathStatus::OutsideVolume;
            return result;
        }

        glm::vec3 startPoint = request.start;
        if (!grid.isNavigable(*startCoord))
        {
            const std::optional<glm::vec3> snapped = snapToNavigable(grid, request.start, request.snapRadius);
            if (!snapped)
            {
                result.status = VolumePathStatus::StartBlocked;
                return result;
            }
            startPoint = *snapped;
            result.startSnapped = *snapped;
            startCoord = grid.coordAt(*snapped);
        }

        glm::vec3 endPoint = request.end;
        if (!grid.isNavigable(*endCoord))
        {
            const std::optional<glm::vec3> snapped = snapToNavigable(grid, request.end, request.snapRadius);
            if (!snapped)
            {
                result.status = VolumePathStatus::EndBlocked;
                return result;
            }
            endPoint = *snapped;
            result.endSnapped = *snapped;
            endCoord = grid.coordAt(*snapped);
        }

        if (!startCoord || !endCoord)
        {
            result.status = VolumePathStatus::OutsideVolume; // a snapped point always lies inside; defensive
            return result;
        }
        if (*startCoord == *endCoord)
        {
            result.status = VolumePathStatus::Found;
            result.waypoints = {startPoint, endPoint};
            return result;
        }

        const float voxelSize = grid.voxelSize();
        const int64_t startIndex = grid.index(*startCoord);
        const int64_t goalIndex = grid.index(*endCoord);

        std::unordered_map<int64_t, NodeRecord> nodes;
        std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> open;

        nodes[startIndex].g = 0.0f;
        open.push(OpenEntry{coordDistance(*startCoord, *endCoord) * voxelSize, 0.0f, startIndex});

        int64_t bestIndex = startIndex;
        float bestHeuristic = coordDistance(*startCoord, *endCoord) * voxelSize;
        bool reachedGoal = false;
        uint32_t expanded = 0;
        std::vector<VoxelCoord> neighbours;
        neighbours.reserve(26);

        while (!open.empty())
        {
            const OpenEntry entry = open.top();
            open.pop();

            NodeRecord& record = nodes[entry.index];
            if (record.closed || entry.g > record.g)
                continue; // a stale entry
            if (expanded >= request.maxNodes)
                break;
            record.closed = true;
            ++expanded;

            if (entry.index == goalIndex)
            {
                reachedGoal = true;
                break;
            }

            const VoxelCoord coord = grid.coordOf(entry.index);
            const float heuristic = coordDistance(coord, *endCoord) * voxelSize;
            if (heuristic < bestHeuristic)
            {
                bestHeuristic = heuristic;
                bestIndex = entry.index;
            }

            grid.neighbors(coord, neighbours);
            for (const VoxelCoord& next : neighbours)
            {
                const int64_t nextIndex = grid.index(next);
                const float tentative = entry.g + coordDistance(coord, next) * voxelSize;
                NodeRecord& nextRecord = nodes[nextIndex];
                if (nextRecord.closed || tentative >= nextRecord.g)
                    continue;
                nextRecord.g = tentative;
                nextRecord.parent = entry.index;
                open.push(OpenEntry{tentative + coordDistance(next, *endCoord) * voxelSize, tentative, nextIndex});
            }
        }
        result.nodesExpanded = expanded;

        const int64_t lastIndex = reachedGoal ? goalIndex : bestIndex;
        if (!reachedGoal && lastIndex == startIndex)
        {
            result.status = VolumePathStatus::NoPath;
            return result;
        }

        std::vector<glm::vec3> points;
        for (int64_t index = lastIndex; index != -1; index = nodes[index].parent)
        {
            points.push_back(grid.centerOf(grid.coordOf(index)));
            if (index == startIndex)
                break;
        }
        std::reverse(points.begin(), points.end());
        points.front() = startPoint;
        if (reachedGoal)
            points.back() = endPoint;

        result.status = reachedGoal ? VolumePathStatus::Found : VolumePathStatus::Partial;
        result.waypoints = smoothPath(grid, points);
        return result;
    }
}
