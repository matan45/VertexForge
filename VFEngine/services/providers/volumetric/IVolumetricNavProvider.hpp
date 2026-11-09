#pragma once
#include "navigation/volumetric/VolumetricNavWorld.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace services
{
    // The loaded voxel volumes (rewritten for VK-1660): a thin owner of a volumetric::VolumetricNavWorld, keyed by
    // the volume entity's UUID. Thread-safe.
    class IVolumetricNavProvider
    {
    public:
        virtual ~IVolumetricNavProvider() = default;

        virtual void setVolume(uint64_t volumeId, std::shared_ptr<const volumetric::VoxelGrid> grid) = 0;
        virtual void removeVolume(uint64_t volumeId) = 0;
        virtual void clearVolumes() = 0;
        virtual std::shared_ptr<const volumetric::VoxelGrid> getVolume(uint64_t volumeId) const = 0;
        virtual std::vector<uint64_t> getVolumeIds() const = 0;
        virtual uint64_t volumeAt(const glm::vec3& point) const = 0;
        virtual volumetric::VolumePathResult findPath3D(const volumetric::PathRequest& request) const = 0;
        virtual volumetric::VolumetricNavWorld::PointQuery queryPoint(const glm::vec3& point, float snapRadius) const = 0;
    };
}
