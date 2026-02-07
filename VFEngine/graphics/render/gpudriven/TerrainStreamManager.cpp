#include "TerrainStreamManager.hpp"
#include "TerrainMeshBuffer.hpp"
#include "terrain/TerrainTile.hpp"
#include <algorithm>

namespace render::gpudriven
{
    TerrainStreamManager::TerrainStreamManager(TerrainMeshBuffer& terrainBuf,
                                               TerrainGPUAdapter& gpuAdapter)
        : terrainBuffer(terrainBuf)
        , adapter(gpuAdapter)
    {
    }

    TerrainStreamManager::~TerrainStreamManager()
    {
        clear();
    }

    void TerrainStreamManager::update(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                      const glm::vec3& cameraPosition)
    {
        currentFrame++;

        stats.uploadsThisFrame = 0;
        stats.bytesUploadedThisFrame = 0;
        stats.tilesStreaming = 0;

        std::unordered_map<TerrainTileKey, terrain::TerrainTile*, TerrainTileKeyHash> tileMap;
        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (tile && tile->isVisible)
            {
                tileMap[{tile->coord.x, tile->coord.z}] = tile;
            }
        }

        // Sort visible tiles by distance to camera (nearest first) for fallback budgeting
        struct TileWithDistance
        {
            terrain::TerrainTile* tile;
            float distance;
        };
        std::vector<TileWithDistance> sortedTiles;
        sortedTiles.reserve(visibleTiles.size());

        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->isVisible)
                continue;

            glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
            float distance = glm::length(tileCenter - cameraPosition);
            sortedTiles.push_back({tile, distance});
        }

        std::sort(sortedTiles.begin(), sortedTiles.end(),
                  [](const TileWithDistance& a, const TileWithDistance& b) {
                      return a.distance < b.distance;
                  });

        uint32_t fallbackUploads = 0;
        size_t fallbackBytes = 0;

        for (const auto& entry : sortedTiles)
        {
            terrain::TerrainTile* tile = entry.tile;
            float distance = entry.distance;

            TerrainTileKey key{tile->coord.x, tile->coord.z};

            auto infoIt = tileInfos.find(key);
            if (infoIt == tileInfos.end())
            {
                TerrainTileStreamInfo info;
                info.key = key;
                info.state = TerrainTileStreamState::NotLoaded;
                info.currentLoadedLOD = 255;
                info.distanceToCamera = distance;
                info.targetLOD = selectTargetLOD(distance);
                info.lastAccessFrame = currentFrame;
                auto result = tileInfos.emplace(key, info);
                infoIt = result.first;
            }
            else
            {
                infoIt->second.distanceToCamera = distance;
                infoIt->second.targetLOD = selectTargetLOD(distance);
                infoIt->second.lastAccessFrame = currentFrame;
            }

            if (!infoIt->second.hasLODLoaded(3))
            {
                if (fallbackUploads >= config.maxFallbackUploadsPerFrame ||
                    fallbackBytes >= config.maxFallbackBytesPerFrame)
                {
                    continue;
                }

                if (adapter.uploadTileAddLOD(*tile, 3))
                {
                    infoIt->second.setLODLoaded(3);
                    if (infoIt->second.currentLoadedLOD == 255)
                    {
                        infoIt->second.currentLoadedLOD = 3;
                    }
                    infoIt->second.state = TerrainTileStreamState::FallbackOnly;

                    size_t lodMemory = estimateLODMemory(*tile, 3);
                    infoIt->second.gpuMemoryUsage += lodMemory;
                    currentMemoryUsage += lodMemory;
                    stats.uploadsThisFrame++;
                    stats.bytesUploadedThisFrame += lodMemory;
                    fallbackUploads++;
                    fallbackBytes += lodMemory;
                }
            }

            // Upload weight map only on initial load or when dirty
            if (tile->hasWeightMap() && tile->weightMapGPUDirty)
            {
                if (adapter.uploadWeightMap(*tile))
                {
                    tile->weightMapGPUDirty = false;
                }
            }
        }

        // Handle dirty tiles (brush sculpting) - re-upload regenerated LODs
        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->hasAnyGPUDirtyLOD())
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto infoIt = tileInfos.find(key);

            for (uint8_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (!tile->isLODGPUDirty(lod))
                    continue;

                // Only re-upload if this LOD was previously loaded on GPU
                if (infoIt != tileInfos.end() && infoIt->second.hasLODLoaded(lod))
                {
                    // Evict stale GPU data
                    evictTileLOD(key, lod);

                    // Re-upload with regenerated meshlet data
                    if (adapter.uploadTileAddLOD(*tile, lod))
                    {
                        auto& info = tileInfos[key];
                        info.setLODLoaded(lod);
                        size_t mem = estimateLODMemory(*tile, lod);
                        info.gpuMemoryUsage += mem;
                        currentMemoryUsage += mem;
                        stats.uploadsThisFrame++;
                        stats.bytesUploadedThisFrame += mem;
                    }
                }

                tile->clearLODGPUDirty(lod);
            }
        }

        // Handle weight map dirty (paint brush updates)
        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->weightMapGPUDirty || !tile->hasWeightMap())
                continue;

            adapter.uploadWeightMap(*tile);
            tile->weightMapGPUDirty = false;
        }

        while (!uploadQueue.empty())
        {
            uploadQueue.pop();
        }

        for (auto& [key, info] : tileInfos)
        {
            if (info.lastAccessFrame != currentFrame)
                continue;

            uint8_t targetLOD = info.targetLOD;

            for (uint8_t lod = 0; lod < 3; ++lod)
            {
                if (lod <= targetLOD && !info.hasLODLoaded(lod))
                {
                    float priority = calculatePriority(info.distanceToCamera, lod, info.currentLoadedLOD);
                    uploadQueue.push({key, lod, priority});
                }
            }
        }

        size_t bytesUploaded = 0;
        uint32_t uploadsCount = 0;

        while (!uploadQueue.empty() &&
               uploadsCount < config.maxUploadsPerFrame &&
               bytesUploaded < config.maxBytesPerFrame)
        {
            if (currentMemoryUsage >= config.memoryBudgetBytes * config.evictionThreshold)
            {
                break;
            }

            StreamPriorityEntry entry = uploadQueue.top();
            uploadQueue.pop();

            auto tileIt = tileMap.find(entry.key);
            if (tileIt == tileMap.end())
                continue;

            terrain::TerrainTile* tile = tileIt->second;
            if (!tile)
                continue;

            auto infoIt = tileInfos.find(entry.key);
            if (infoIt == tileInfos.end() || infoIt->second.hasLODLoaded(entry.targetLOD))
                continue;

            size_t lodMemory = estimateLODMemory(*tile, entry.targetLOD);

            if (currentMemoryUsage + lodMemory > config.memoryBudgetBytes)
                continue;

            if (adapter.uploadTileAddLOD(*tile, entry.targetLOD))
            {
                infoIt->second.setLODLoaded(entry.targetLOD);

                if (entry.targetLOD < infoIt->second.currentLoadedLOD)
                {
                    infoIt->second.currentLoadedLOD = entry.targetLOD;
                }

                if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD)
                {
                    infoIt->second.state = TerrainTileStreamState::FullyLoaded;
                }
                else
                {
                    infoIt->second.state = TerrainTileStreamState::Streaming;
                    stats.tilesStreaming++;
                }

                infoIt->second.gpuMemoryUsage += lodMemory;
                currentMemoryUsage += lodMemory;
                bytesUploaded += lodMemory;
                uploadsCount++;
                stats.uploadsThisFrame++;
                stats.bytesUploadedThisFrame += lodMemory;
            }
        }

        processEvictions(cameraPosition);

        stats.memoryUsedBytes = currentMemoryUsage;
        stats.memoryBudgetBytes = config.memoryBudgetBytes;
        stats.tilesLoaded = static_cast<uint32_t>(tileInfos.size());

        stats.fallbackTiles = 0;
        stats.fullDetailTiles = 0;
        for (const auto& [key, info] : tileInfos)
        {
            if (info.loadedLODMask == 0)
                continue;

            if (info.currentLoadedLOD == 0)
                stats.fullDetailTiles++;
            else if (info.currentLoadedLOD == 3)
                stats.fallbackTiles++;
        }
    }

    uint8_t TerrainStreamManager::selectTargetLOD(float distance) const
    {
        if (distance < 50.0f)  return 0;
        if (distance < 150.0f) return 1;
        if (distance < 300.0f) return 2;
        return 3;
    }

    float TerrainStreamManager::calculatePriority(float distance,
                                                  uint8_t targetLOD,
                                                  uint8_t currentLOD) const
    {
        float distancePriority = 1.0f / (1.0f + distance * 0.01f);

        float lodUrgency = 1.0f;
        if (currentLOD != 255 && targetLOD < currentLOD)
        {
            lodUrgency = 2.0f + (currentLOD - targetLOD) * 0.5f;
        }
        else if (currentLOD == 255)
        {
            lodUrgency = 3.0f;
        }

        return distancePriority * lodUrgency;
    }

    void TerrainStreamManager::processEvictions(const glm::vec3& cameraPosition)
    {
        if (currentMemoryUsage < config.memoryBudgetBytes * config.evictionThreshold)
        {
            return;
        }

        struct EvictionCandidate
        {
            TerrainTileKey key;
            uint8_t lodLevel;
            float evictionScore; // Higher = more likely to evict
            size_t memorySize;
        };

        std::vector<EvictionCandidate> candidates;

        for (auto& [key, info] : tileInfos)
        {
            if (info.lastAccessFrame == currentFrame)
                continue;

            for (uint8_t lod = 0; lod < (config.keepFallbackLoaded ? 3 : 4); ++lod)
            {
                if (!info.hasLODLoaded(lod))
                    continue;

                uint64_t age = currentFrame - info.lastAccessFrame;
                float evictionScore = info.distanceToCamera * static_cast<float>(age);
                evictionScore *= (1.0f + lod * 0.25f);

                candidates.push_back({key, lod, evictionScore, LOD_MEMORY_ESTIMATE[lod]});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const EvictionCandidate& a, const EvictionCandidate& b) {
                      return a.evictionScore > b.evictionScore;
                  });

        size_t targetMemory = static_cast<size_t>(config.memoryBudgetBytes * 0.8f); // Target 80% usage

        for (const auto& candidate : candidates)
        {
            if (currentMemoryUsage <= targetMemory)
                break;

            evictTileLOD(candidate.key, candidate.lodLevel);
            stats.tilesEvicted++;
        }
    }

    void TerrainStreamManager::evictTileLOD(const TerrainTileKey& key, uint8_t lodLevel)
    {
        auto infoIt = tileInfos.find(key);
        if (infoIt == tileInfos.end())
            return;

        auto& info = infoIt->second;
        if (!info.hasLODLoaded(lodLevel))
            return;

        adapter.removeTileLOD(key, lodLevel);

        size_t lodMemory = LOD_MEMORY_ESTIMATE[lodLevel];
        currentMemoryUsage -= std::min(lodMemory, info.gpuMemoryUsage);
        info.gpuMemoryUsage -= std::min(lodMemory, info.gpuMemoryUsage);
        info.clearLODLoaded(lodLevel);

        if (info.loadedLODMask == 0)
        {
            info.currentLoadedLOD = 255;
            info.state = TerrainTileStreamState::NotLoaded;
        }
        else
        {
            // Update current loaded LOD to finest available
            for (uint8_t lod = 0; lod < 4; ++lod)
            {
                if (info.hasLODLoaded(lod))
                {
                    info.currentLoadedLOD = lod;
                    break;
                }
            }

            if (info.currentLoadedLOD == 3)
            {
                info.state = TerrainTileStreamState::FallbackOnly;
            }
            else
            {
                info.state = TerrainTileStreamState::Streaming;
            }
        }
    }

    size_t TerrainStreamManager::estimateLODMemory(const terrain::TerrainTile& tile, uint8_t lodLevel) const
    {
        if (lodLevel >= 4)
            return 0;

        const auto& lodData = tile.lodLevels[lodLevel];
        if (lodData.isEmpty())
            return 0;

        size_t vertexMemory = lodData.vertices.size() * sizeof(resource::Vertex);
        size_t indexMemory = lodData.indices.size() * sizeof(uint32_t);
        size_t meshletMemory = lodData.meshlets.size() * sizeof(GPUMeshlet);
        size_t meshletVertexMemory = lodData.meshletVertices.size() * sizeof(uint32_t);
        size_t meshletPrimitiveMemory = lodData.meshletPrimitives.size() * sizeof(uint32_t);

        return vertexMemory + indexMemory + meshletMemory + meshletVertexMemory + meshletPrimitiveMemory;
    }

    void TerrainStreamManager::clear()
    {
        adapter.clear();

        tileInfos.clear();
        while (!uploadQueue.empty())
        {
            uploadQueue.pop();
        }

        currentMemoryUsage = 0;
        stats = TerrainStreamingStats{};
    }

}
