#include "GrassStreamManager.hpp"
#include <algorithm>
#include <cmath>

namespace render::vegetation
{
    void GrassStreamManager::init(core::Device& device, VegetationBufferManager& bufferManager)
    {
        devicePtr = &device;
        bufferManagerPtr = &bufferManager;
    }

    void GrassStreamManager::cleanup()
    {
        tileStates.clear();
        pendingUploads.clear();
        currentMemoryUsage = 0;
        devicePtr = nullptr;
        bufferManagerPtr = nullptr;
    }

    void GrassStreamManager::update(const glm::vec3& cameraPos)
    {
        if (!bufferManagerPtr) return;

        // Recalculate priorities
        for (auto& [key, state] : tileStates)
        {
            state.priority = calculatePriority(key, cameraPos);
        }

        // Collect dirty/pending tiles
        pendingUploads.clear();
        for (auto& [key, state] : tileStates)
        {
            if (state.isDirty || !state.isUploaded)
            {
                pendingUploads.push_back(key);
            }
        }

        // Sort by priority (highest first)
        std::sort(pendingUploads.begin(), pendingUploads.end(),
            [this](const VegetationTileKey& a, const VegetationTileKey& b)
            {
                return tileStates[a].priority > tileStates[b].priority;
            });

        // Process limited uploads per frame
        uint32_t uploadsThisFrame = 0;
        for (const auto& key : pendingUploads)
        {
            if (uploadsThisFrame >= streamConfig.maxUploadsPerFrame) break;

            auto& state = tileStates[key];
            // Actual upload logic will be connected to density map → compute shader pipeline
            state.isUploaded = true;
            state.isDirty = false;
            // Estimate memory usage per tile for budget tracking
            constexpr uint64_t estimatedBytesPerTile = 64 * 1024; // 64KB per tile
            state.memoryUsage = estimatedBytesPerTile;
            currentMemoryUsage += state.memoryUsage;
            uploadsThisFrame++;
        }

        // Eviction if over budget
        if (currentMemoryUsage > static_cast<uint64_t>(
                static_cast<double>(streamConfig.memoryBudgetBytes) * streamConfig.evictionThreshold))
        {
            // Find lowest priority uploaded tiles and evict
            std::vector<VegetationTileKey> evictionCandidates;
            for (const auto& [key, state] : tileStates)
            {
                if (state.isUploaded)
                {
                    evictionCandidates.push_back(key);
                }
            }

            std::sort(evictionCandidates.begin(), evictionCandidates.end(),
                [this](const VegetationTileKey& a, const VegetationTileKey& b)
                {
                    return tileStates[a].priority < tileStates[b].priority;
                });

            for (const auto& key : evictionCandidates)
            {
                if (currentMemoryUsage <= streamConfig.memoryBudgetBytes * 0.7)
                    break;

                auto& state = tileStates[key];
                bufferManagerPtr->freeGrassInstances(key);
                currentMemoryUsage -= state.memoryUsage;
                state.memoryUsage = 0;
                state.isUploaded = false;
            }
        }
    }

    void GrassStreamManager::markTileDirty(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        auto it = tileStates.find(key);
        if (it != tileStates.end())
        {
            it->second.isDirty = true;
        }
    }

    void GrassStreamManager::addTile(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        if (!tileStates.contains(key))
        {
            TileStreamState state{};
            state.key = key;
            tileStates[key] = state;
        }
    }

    void GrassStreamManager::removeTile(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        auto it = tileStates.find(key);
        if (it != tileStates.end())
        {
            if (it->second.isUploaded && bufferManagerPtr)
            {
                bufferManagerPtr->freeGrassInstances(key);
                currentMemoryUsage -= it->second.memoryUsage;
            }
            tileStates.erase(it);
        }
    }

    float GrassStreamManager::calculatePriority(const VegetationTileKey& key, const glm::vec3& cameraPos) const
    {
        float tileSize = streamConfig.worldTileSize;
        float tileX = static_cast<float>(key.coordX) * tileSize + tileSize * 0.5f;
        float tileZ = static_cast<float>(key.coordZ) * tileSize + tileSize * 0.5f;

        float dx = cameraPos.x - tileX;
        float dz = cameraPos.z - tileZ;
        float dist = std::sqrt(dx * dx + dz * dz);

        return 1.0f / (1.0f + dist * 0.01f);
    }
}
