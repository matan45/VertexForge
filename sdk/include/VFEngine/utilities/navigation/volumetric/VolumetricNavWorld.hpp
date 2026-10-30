#pragma once
#include "VoxelGrid.hpp"
#include "VolumetricPathfinder.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace volumetric
{
    // Every loaded volume of the scene, keyed by its entity's UUID (stable across a scene reload and Play->Stop).
    // Thread-safe: agents and queries read while a bake or a load replaces a volume.
    class VolumetricNavWorld
    {
    public:
        struct PointQuery
        {
            uint64_t volumeId = 0;          // the volume holding the point (0 = none)
            bool navigable = false;         // the point's voxel is navigable
            std::optional<glm::vec3> nearest; // the point, or the nearest navigable voxel within snapRadius
        };

        void setVolume(uint64_t volumeId, std::shared_ptr<const VoxelGrid> grid); // replaces; null removes
        void removeVolume(uint64_t volumeId);
        void clear();

        std::shared_ptr<const VoxelGrid> getVolume(uint64_t volumeId) const;
        std::vector<uint64_t> volumeIds() const; // ascending

        // The smallest volume (fewest voxels; lowest id on a tie) whose grid contains `point`; 0 = none.
        uint64_t volumeAt(const glm::vec3& point) const;

        // NoVolume when nothing is loaded; OutsideVolume when the start or the end is in no volume;
        // DifferentVolumes when no single volume contains both; otherwise findPath on the smallest volume that
        // contains both, with volumeId set.
        VolumePathResult findPath(const PathRequest& request) const;

        PointQuery queryPoint(const glm::vec3& point, float snapRadius) const;

    private:
        mutable std::shared_mutex mutex;
        std::map<uint64_t, std::shared_ptr<const VoxelGrid>> volumes;
    };
}
