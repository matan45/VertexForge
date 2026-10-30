#include "VolumetricNavWorld.hpp"
#include <limits>
#include <mutex>
#include <tuple>
#include <utility>

namespace volumetric
{
    namespace
    {
        // Of the volumes for which `contains` holds, the one with the fewest voxels (lowest id on a tie: the map
        // iterates ids in ascending order and only a strictly smaller count replaces the pick).
        template <typename Contains>
        std::pair<uint64_t, std::shared_ptr<const VoxelGrid>> smallestVolume(
            const std::map<uint64_t, std::shared_ptr<const VoxelGrid>>& volumes, Contains contains)
        {
            uint64_t bestId = 0;
            std::shared_ptr<const VoxelGrid> best;
            int64_t bestCount = std::numeric_limits<int64_t>::max();
            for (const auto& [id, grid] : volumes)
            {
                if (!grid || !contains(*grid))
                    continue;
                if (grid->voxelCount() < bestCount)
                {
                    bestCount = grid->voxelCount();
                    bestId = id;
                    best = grid;
                }
            }
            return {bestId, best};
        }
    }

    void VolumetricNavWorld::setVolume(uint64_t volumeId, std::shared_ptr<const VoxelGrid> grid)
    {
        if (volumeId == 0)
            return; // 0 means "no volume" in every query result
        std::unique_lock lock(mutex);
        if (grid)
            volumes[volumeId] = std::move(grid);
        else
            volumes.erase(volumeId);
    }

    void VolumetricNavWorld::removeVolume(uint64_t volumeId)
    {
        std::unique_lock lock(mutex);
        volumes.erase(volumeId);
    }

    void VolumetricNavWorld::clear()
    {
        std::unique_lock lock(mutex);
        volumes.clear();
    }

    std::shared_ptr<const VoxelGrid> VolumetricNavWorld::getVolume(uint64_t volumeId) const
    {
        std::shared_lock lock(mutex);
        auto it = volumes.find(volumeId);
        return it != volumes.end() ? it->second : nullptr;
    }

    std::vector<uint64_t> VolumetricNavWorld::volumeIds() const
    {
        std::shared_lock lock(mutex);
        std::vector<uint64_t> ids;
        ids.reserve(volumes.size());
        for (const auto& entry : volumes)
            ids.push_back(entry.first);
        return ids;
    }

    uint64_t VolumetricNavWorld::volumeAt(const glm::vec3& point) const
    {
        std::shared_lock lock(mutex);
        return smallestVolume(volumes, [&point](const VoxelGrid& grid) { return grid.coordAt(point).has_value(); }).first;
    }

    VolumePathResult VolumetricNavWorld::findPath(const PathRequest& request) const
    {
        VolumePathResult result;
        uint64_t volumeId = 0;
        std::shared_ptr<const VoxelGrid> grid;
        {
            std::shared_lock lock(mutex);
            if (volumes.empty())
            {
                result.status = VolumePathStatus::NoVolume;
                return result;
            }

            bool startInside = false;
            bool endInside = false;
            for (const auto& entry : volumes)
            {
                if (!entry.second)
                    continue;
                startInside = startInside || entry.second->coordAt(request.start).has_value();
                endInside = endInside || entry.second->coordAt(request.end).has_value();
            }
            if (!startInside || !endInside)
            {
                result.status = VolumePathStatus::OutsideVolume;
                return result;
            }

            std::tie(volumeId, grid) = smallestVolume(volumes, [&request](const VoxelGrid& candidate)
            {
                return candidate.coordAt(request.start).has_value() && candidate.coordAt(request.end).has_value();
            });
            if (!grid)
            {
                result.status = VolumePathStatus::DifferentVolumes;
                return result;
            }
        }

        // The grid is immutable and shared: the search runs without holding the lock.
        result = volumetric::findPath(*grid, request);
        result.volumeId = volumeId;
        return result;
    }

    VolumetricNavWorld::PointQuery VolumetricNavWorld::queryPoint(const glm::vec3& point, float snapRadius) const
    {
        PointQuery query;
        std::shared_ptr<const VoxelGrid> grid;
        {
            std::shared_lock lock(mutex);
            std::tie(query.volumeId, grid) =
                smallestVolume(volumes, [&point](const VoxelGrid& candidate) { return candidate.coordAt(point).has_value(); });
        }
        if (!grid)
            return query;

        const std::optional<VoxelCoord> coord = grid->coordAt(point);
        query.navigable = coord && grid->isNavigable(*coord);
        query.nearest = snapToNavigable(*grid, point, snapRadius);
        return query;
    }
}
