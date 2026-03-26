#include "VolumetricNavAdapter.hpp"
#include "print/Log.hpp"
#include <cmath>

namespace core
{
    VolumetricNavAdapter::VolumetricNavAdapter()
        : octree(std::make_unique<volumetric::SparseVoxelOctree>()),
          pathfinder(std::make_unique<volumetric::VolumetricPathfinder>())
    {
    }

    VolumetricNavAdapter::~VolumetricNavAdapter()
    {
        if (bakeFuture.valid())
        {
            bakeFuture.wait();
        }
    }

    bool VolumetricNavAdapter::bakeVolume(glm::vec3 boundsMin, glm::vec3 boundsMax, float voxelSize,
                                           uint8_t connectivity, float agentClearance,
                                           const std::function<bool(glm::vec3, float)>& isBlocked)
    {
        updateProgress(volumetric::VolumetricBakeStatus::Voxelizing, 0.0f, "Voxelizing volume...");

        glm::vec3 size = boundsMax - boundsMin;
        int dimX = static_cast<int>(std::ceil(size.x / voxelSize));
        int dimY = static_cast<int>(std::ceil(size.y / voxelSize));
        int dimZ = static_cast<int>(std::ceil(size.z / voxelSize));

        if (dimX <= 0 || dimY <= 0 || dimZ <= 0)
        {
            updateProgress(volumetric::VolumetricBakeStatus::Failed, 0.0f, "Invalid volume dimensions");
            return false;
        }

        int totalVoxels = dimX * dimY * dimZ;
        std::vector<uint8_t> denseGrid(totalVoxels, 1); // Default navigable

        float halfSize = agentClearance;
        int processed = 0;

        for (int z = 0; z < dimZ; ++z)
        {
            for (int y = 0; y < dimY; ++y)
            {
                for (int x = 0; x < dimX; ++x)
                {
                    glm::vec3 center = boundsMin + glm::vec3(
                        (x + 0.5f) * voxelSize,
                        (y + 0.5f) * voxelSize,
                        (z + 0.5f) * voxelSize);

                    if (isBlocked(center, halfSize))
                    {
                        int idx = x + y * dimX + z * dimX * dimY;
                        denseGrid[idx] = 0;
                    }

                    processed++;
                    if (processed % 10000 == 0)
                    {
                        float prog = static_cast<float>(processed) / static_cast<float>(totalVoxels);
                        updateProgress(volumetric::VolumetricBakeStatus::Voxelizing, prog * 0.8f, "Voxelizing volume...");
                    }
                }
            }
        }

        updateProgress(volumetric::VolumetricBakeStatus::BuildingOctree, 0.8f, "Building octree...");

        {
            std::lock_guard lock(mutex);
            octree->build(denseGrid, glm::ivec3(dimX, dimY, dimZ), boundsMin, voxelSize);
        }

        updateProgress(volumetric::VolumetricBakeStatus::Complete, 1.0f, "Complete");
        return true;
    }

    volumetric::VolumePath VolumetricNavAdapter::findPath3D(glm::vec3 start, glm::vec3 end)
    {
        std::lock_guard lock(mutex);
        if (octree->getNodeCount() == 0)
        {
            return volumetric::VolumePath{};
        }
        return pathfinder->findPath(*octree, start, end);
    }

    bool VolumetricNavAdapter::isPointNavigable(glm::vec3 point)
    {
        std::lock_guard lock(mutex);
        if (octree->getNodeCount() == 0)
        {
            return false;
        }
        auto coord = octree->worldToVoxel(point);
        return octree->isInBounds(coord) && octree->isNavigable(coord);
    }

    void VolumetricNavAdapter::clear()
    {
        std::lock_guard lock(mutex);
        octree->clear();
        updateProgress(volumetric::VolumetricBakeStatus::Idle, 0.0f, "");
    }

    bool VolumetricNavAdapter::hasVolume() const
    {
        std::lock_guard lock(mutex);
        return octree->getNodeCount() > 0;
    }

    volumetric::VolumetricBakeProgress VolumetricNavAdapter::getBakeProgress() const
    {
        std::lock_guard lock(progressMutex);
        return currentProgress;
    }

    void VolumetricNavAdapter::updateProgress(volumetric::VolumetricBakeStatus status, float progress,
                                               const std::string& stage)
    {
        std::lock_guard lock(progressMutex);
        currentProgress.status = status;
        currentProgress.progress = progress;
        currentProgress.currentStage = stage;
    }
}
