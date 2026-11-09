#pragma once

#include "../../../services/providers/volumetric/IVolumetricNavProvider.hpp"
#include "navigation/volumetric/VolumetricNavWorld.hpp"

namespace core
{
    // VK-1660: the loaded voxel volumes, one per VolumetricNavVolume entity (keyed by its UUID). A thin owner of a
    // volumetric::VolumetricNavWorld, which does the locking: bakes and loads replace grids while agents and
    // queries read them.
    class VolumetricNavAdapter : public services::IVolumetricNavProvider
    {
    public:
        VolumetricNavAdapter() = default;
        ~VolumetricNavAdapter() override = default;

        VolumetricNavAdapter(const VolumetricNavAdapter&) = delete;
        VolumetricNavAdapter& operator=(const VolumetricNavAdapter&) = delete;

        void setVolume(uint64_t volumeId, std::shared_ptr<const volumetric::VoxelGrid> grid) override;
        void removeVolume(uint64_t volumeId) override;
        void clearVolumes() override;
        std::shared_ptr<const volumetric::VoxelGrid> getVolume(uint64_t volumeId) const override;
        std::vector<uint64_t> getVolumeIds() const override;
        uint64_t volumeAt(const glm::vec3& point) const override;
        volumetric::VolumePathResult findPath3D(const volumetric::PathRequest& request) const override;
        volumetric::VolumetricNavWorld::PointQuery queryPoint(const glm::vec3& point, float snapRadius) const override;

    private:
        volumetric::VolumetricNavWorld world;
    };
}
