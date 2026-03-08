#include "VegetationStreamManager.hpp"
#include <algorithm>
#include <cmath>

namespace render::vegetation
{
    void VegetationStreamManager::init(core::Device& device, VegetationBufferManager& bufferManager)
    {
        devicePtr = &device;
        bufferManagerPtr = &bufferManager;
    }

    void VegetationStreamManager::cleanup()
    {
        tileStates.clear();
        currentMemoryUsage = 0;
        devicePtr = nullptr;
        bufferManagerPtr = nullptr;
    }

    void VegetationStreamManager::update(const glm::vec3& cameraPos)
    {
        if (!bufferManagerPtr) return;

        // Recalculate priorities
        for (auto& [key, state] : tileStates)
        {
            state.priority = calculatePriority(key, cameraPos);
        }

        // Collect pending uploads
        std::vector<VegetationTileKey> pending;
        for (auto& [key, state] : tileStates)
        {
            if (state.isDirty || !state.isUploaded)
            {
                pending.push_back(key);
            }
        }

        std::sort(pending.begin(), pending.end(),
            [this](const VegetationTileKey& a, const VegetationTileKey& b)
            {
                return tileStates[a].priority > tileStates[b].priority;
            });

        uint32_t uploadsThisFrame = 0;
        for (const auto& key : pending)
        {
            if (uploadsThisFrame >= streamConfig.maxUploadsPerFrame) break;

            auto& state = tileStates[key];
            state.isUploaded = true;
            state.isDirty = false;
            uploadsThisFrame++;
        }

        // Eviction if over budget
        if (currentMemoryUsage > static_cast<uint64_t>(
                static_cast<double>(streamConfig.memoryBudgetBytes) * streamConfig.evictionThreshold))
        {
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
                if (currentMemoryUsage <= static_cast<uint64_t>(streamConfig.memoryBudgetBytes * 0.7))
                    break;

                auto& state = tileStates[key];
                bufferManagerPtr->freeTile(key);
                currentMemoryUsage -= state.memoryUsage;
                state.memoryUsage = 0;
                state.isUploaded = false;
            }
        }
    }

    void VegetationStreamManager::addTile(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        if (!tileStates.contains(key))
        {
            TileStreamState state{};
            state.key = key;
            tileStates[key] = state;
        }
    }

    void VegetationStreamManager::removeTile(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        auto it = tileStates.find(key);
        if (it != tileStates.end())
        {
            if (it->second.isUploaded && bufferManagerPtr)
            {
                bufferManagerPtr->freeTile(key);
                currentMemoryUsage -= it->second.memoryUsage;
            }
            tileStates.erase(it);
        }
    }

    void VegetationStreamManager::markTileDirty(int32_t coordX, int32_t coordZ)
    {
        VegetationTileKey key{coordX, coordZ};
        auto it = tileStates.find(key);
        if (it != tileStates.end())
        {
            it->second.isDirty = true;
        }
    }

    float VegetationStreamManager::calculatePriority(const VegetationTileKey& key, const glm::vec3& cameraPos) const
    {
        constexpr float tileSize = 32.0f;
        float tileX = static_cast<float>(key.coordX) * tileSize + tileSize * 0.5f;
        float tileZ = static_cast<float>(key.coordZ) * tileSize + tileSize * 0.5f;

        float dx = cameraPos.x - tileX;
        float dz = cameraPos.z - tileZ;
        float dist = std::sqrt(dx * dx + dz * dz);

        return 1.0f / (1.0f + dist * 0.01f);
    }

    void VegetationStreamManager::addBillboardRequest(const VegetationBillboardRequest& request)
    {
        billboardRequests.push_back(request);
    }

    void VegetationStreamManager::clearBillboardRequests()
    {
        billboardRequests.clear();
        billboardInstances.clear();
    }

    void VegetationStreamManager::buildBillboardInstances()
    {
        billboardInstances.clear();
        billboardInstances.reserve(billboardRequests.size());

        for (const auto& req : billboardRequests)
        {
            gpudriven::BillboardInstanceGPU instance{};
            instance.positionAndScale = glm::vec4(req.position, req.scale);
            instance.atlasUVRect = req.atlasUVRect;
            instance.colorTint = glm::vec4(1.0f); // No tint for vegetation impostors
            instance.bindlessTextureIndex = req.bindlessTextureIndex;
            instance.flags = 1; // FLAG_AXIS_ALIGNED (Y-up for trees)
            instance.entityId = 0;
            instance.rotation = 0.0f;
            instance.size = req.size;
            billboardInstances.push_back(instance);
        }
    }
}
