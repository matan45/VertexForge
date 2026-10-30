#include "VolumetricNavAdapter.hpp"

#include <utility>

namespace core
{
    void VolumetricNavAdapter::setVolume(uint64_t volumeId, std::shared_ptr<const volumetric::VoxelGrid> grid)
    {
        world.setVolume(volumeId, std::move(grid));
    }

    void VolumetricNavAdapter::removeVolume(uint64_t volumeId)
    {
        world.removeVolume(volumeId);
    }

    void VolumetricNavAdapter::clearVolumes()
    {
        world.clear();
    }

    std::shared_ptr<const volumetric::VoxelGrid> VolumetricNavAdapter::getVolume(uint64_t volumeId) const
    {
        return world.getVolume(volumeId);
    }

    std::vector<uint64_t> VolumetricNavAdapter::getVolumeIds() const
    {
        return world.volumeIds();
    }

    uint64_t VolumetricNavAdapter::volumeAt(const glm::vec3& point) const
    {
        return world.volumeAt(point);
    }

    volumetric::VolumePathResult VolumetricNavAdapter::findPath3D(const volumetric::PathRequest& request) const
    {
        return world.findPath(request);
    }

    volumetric::VolumetricNavWorld::PointQuery VolumetricNavAdapter::queryPoint(const glm::vec3& point,
                                                                               float snapRadius) const
    {
        return world.queryPoint(point, snapRadius);
    }
}
