#pragma once

#include "../../services/providers/volumetric/IVolumetricNavProvider.hpp"
#include "navigation/volumetric/SparseVoxelOctree.hpp"
#include "navigation/volumetric/VolumetricPathfinder.hpp"
#include <memory>
#include <mutex>
#include <future>

namespace core
{
    class VolumetricNavAdapter : public services::IVolumetricNavProvider
    {
    private:
        std::unique_ptr<volumetric::SparseVoxelOctree> octree;
        std::unique_ptr<volumetric::VolumetricPathfinder> pathfinder;

        mutable std::mutex mutex;
        mutable std::mutex progressMutex;
        volumetric::VolumetricBakeProgress currentProgress;
        std::future<bool> bakeFuture;

    public:
        VolumetricNavAdapter();
        ~VolumetricNavAdapter() override;

        VolumetricNavAdapter(const VolumetricNavAdapter&) = delete;
        VolumetricNavAdapter& operator=(const VolumetricNavAdapter&) = delete;

        bool bakeVolume(glm::vec3 boundsMin, glm::vec3 boundsMax, float voxelSize,
                        uint8_t connectivity, float agentClearance,
                        const std::function<bool(glm::vec3, float)>& isBlocked) override;
        volumetric::VolumePath findPath3D(glm::vec3 start, glm::vec3 end) override;
        bool isPointNavigable(glm::vec3 point) override;
        void clear() override;
        bool hasVolume() const override;
        volumetric::VolumetricBakeProgress getBakeProgress() const override;

    private:
        void updateProgress(volumetric::VolumetricBakeStatus status, float progress, const std::string& stage);
    };
}
