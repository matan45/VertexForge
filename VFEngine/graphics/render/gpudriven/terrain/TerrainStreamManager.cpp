#include "TerrainStreamManager.hpp"
#include "TerrainMeshBuffer.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/CaveMeshGenerator.hpp"
#include "threading/JobSystem.hpp"
#include <algorithm>
#include <chrono>

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

        pollCompletions(tileMap);

        uint32_t fileReadsThisFrame = 0;

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

            if (tile->hasWeightMap() && tile->weightMapGPUDirty)
            {
                if (adapter.uploadWeightMap(*tile))
                {
                    tile->weightMapGPUDirty = false;
                }
            }

            if (!infoIt->second.hasLODLoaded(FALLBACK_LOD))
            {
                if (fallbackUploads >= config.maxFallbackUploadsPerFrame ||
                    fallbackBytes >= config.maxFallbackBytesPerFrame)
                {
                    continue;
                }

                if (tile->lodLevels[FALLBACK_LOD].isEmpty())
                {
                    if (tileAsyncDataLoader &&
                        pendingLoads.size() < maxFileReadsPerFrame &&
                        !hasPendingLoad(key))
                    {
                        submitAsyncLoad(key, LOD_MEMORY_ESTIMATE[FALLBACK_LOD]);
                    }
                    continue;
                }

                if (adapter.uploadTileAddLOD(*tile, FALLBACK_LOD))
                {
                    infoIt->second.setLODLoaded(FALLBACK_LOD);
                    if (infoIt->second.currentLoadedLOD == 255)
                    {
                        infoIt->second.currentLoadedLOD = FALLBACK_LOD;
                    }
                    infoIt->second.state = TerrainTileStreamState::FallbackOnly;

                    size_t lodMemory = estimateLODMemory(*tile, FALLBACK_LOD);
                    infoIt->second.gpuMemoryUsage += lodMemory;
                    currentMemoryUsage += lodMemory;
                    stats.uploadsThisFrame++;
                    stats.bytesUploadedThisFrame += lodMemory;
                    fallbackUploads++;
                    fallbackBytes += lodMemory;
                }
            }
        }

        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->hasAnyGPUDirtyLOD())
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto infoIt = tileInfos.find(key);

            for (uint8_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                if (!tile->isLODGPUDirty(lod))
                    continue;

                if (infoIt != tileInfos.end() && infoIt->second.hasLODLoaded(lod))
                {
                    if (tile->lodLevels[lod].isEmpty())
                    {
                        if (!tileDataLoader || fileReadsThisFrame >= maxFileReadsPerFrame)
                            continue;
                        if (!tileDataLoader(*tile, lod))
                            continue;
                        fileReadsThisFrame++;
                    }

                    evictTileLOD(key, lod);

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

        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->weightMapGPUDirty || !tile->hasWeightMap())
                continue;

            if (adapter.uploadWeightMap(*tile))
            {
                tile->weightMapGPUDirty = false;
            }
        }

        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->caveGPUDirty)
                continue;

            // SDF loaded but cave mesh not yet generated
            if (tile->hasCaveData() && tile->caveData->hasCaveGeometry() && tile->caveLOD.isEmpty())
            {
                terrain::CaveMeshGenerator::generate(*tile);
            }

            if (!tile->hasCaveGeometry() || tile->caveLOD.isEmpty())
            {
                tile->caveGPUDirty = false;
                continue;
            }

            if (adapter.uploadCaveMesh(*tile))
            {
                tile->caveGPUDirty = false;
            }
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

            for (uint8_t lod = 0; lod < FALLBACK_LOD; ++lod)
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
            if (currentMemoryUsage + pendingMemoryReserved >= static_cast<size_t>(config.memoryBudgetBytes * config.evictionThreshold))
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

            if (tile->lodLevels[entry.targetLOD].isEmpty())
            {
                if (tileAsyncDataLoader &&
                    pendingLoads.size() < maxFileReadsPerFrame &&
                    !hasPendingLoad(entry.key))
                {
                    submitAsyncLoad(entry.key, lodMemory);
                }
                continue;
            }

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

        processEvictions(cameraPosition, tileMap);

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
            else if (info.currentLoadedLOD == TERRAIN_LOD_LEVEL_COUNT - 1)
                stats.fallbackTiles++;
        }

        stats.pendingAsyncLoads = static_cast<uint32_t>(pendingLoads.size());
        stats.pendingMemoryBytes = pendingMemoryReserved;
    }

    uint8_t TerrainStreamManager::selectTargetLOD(float distance) const
    {
        if (distance < 50.0f)  return 0;
        if (distance < 150.0f) return 1;
        if (distance < 300.0f) return 2;
        if (distance < 500.0f) return 3;
        if (distance < 800.0f) return 4;
        return 5;
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

    void TerrainStreamManager::processEvictions(const glm::vec3& cameraPosition,
                                                 const std::unordered_map<TerrainTileKey, terrain::TerrainTile*, TerrainTileKeyHash>& tileMap)
    {
        if (currentMemoryUsage < config.memoryBudgetBytes * config.evictionThreshold)
        {
            return;
        }

        struct EvictionCandidate
        {
            TerrainTileKey key;
            uint8_t lodLevel;
            float evictionScore;
            size_t memorySize;
        };

        std::vector<EvictionCandidate> candidates;

        for (auto& [key, info] : tileInfos)
        {
            if (info.lastAccessFrame == currentFrame)
                continue;

            for (uint8_t lod = 0; lod < (config.keepFallbackLoaded ? FALLBACK_LOD : TERRAIN_LOD_LEVEL_COUNT); ++lod)
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

        size_t targetMemory = static_cast<size_t>(config.memoryBudgetBytes * 0.8f);

        for (const auto& candidate : candidates)
        {
            if (currentMemoryUsage <= targetMemory)
                break;

            evictTileLOD(candidate.key, candidate.lodLevel);
            cancelPendingLoadsForTile(candidate.key);

            if (tileRAMEvictor)
            {
                auto infoIt = tileInfos.find(candidate.key);
                if (infoIt != tileInfos.end() && infoIt->second.loadedLODMask == 0)
                {
                    auto tileIt = tileMap.find(candidate.key);
                    if (tileIt != tileMap.end() && tileIt->second)
                    {
                        tileRAMEvictor(*tileIt->second);
                    }
                }
            }
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
            for (uint8_t lod = 0; lod < 4; ++lod)
            {
                if (info.hasLODLoaded(lod))
                {
                    info.currentLoadedLOD = lod;
                    break;
                }
            }

            if (info.currentLoadedLOD == FALLBACK_LOD)
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
        if (lodLevel >= TERRAIN_LOD_LEVEL_COUNT)
            return 0;

        const auto& lodData = tile.lodLevels[lodLevel];
        if (lodData.isEmpty())
            return LOD_MEMORY_ESTIMATE[lodLevel]; // Use static estimate for unloaded LODs

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

        pendingLoads.clear();
        pendingMemoryReserved = 0;
        currentMemoryUsage = 0;
        stats = TerrainStreamingStats{};
    }

    void TerrainStreamManager::evictTile(int32_t coordX, int32_t coordZ)
    {
        TerrainTileKey key{coordX, coordZ};

        cancelPendingLoadsForTile(key);

        auto infoIt = tileInfos.find(key);
        if (infoIt == tileInfos.end())
            return;

        for (uint8_t lod = 0; lod < 4; ++lod)
        {
            if (infoIt->second.hasLODLoaded(lod))
            {
                evictTileLOD(key, lod);
            }
        }

        tileInfos.erase(infoIt);
    }

    void TerrainStreamManager::pollCompletions(
        const std::unordered_map<TerrainTileKey, terrain::TerrainTile*, TerrainTileKeyHash>& tileMap)
    {
        uint32_t uploadsThisPoll = 0;

        auto it = pendingLoads.begin();
        while (it != pendingLoads.end())
        {
            if (it->cancelled)
            {
                if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    it->future.get(); // discard
                    pendingMemoryReserved -= std::min(it->estimatedMemory, pendingMemoryReserved);
                    it = pendingLoads.erase(it);
                }
                else
                {
                    ++it;
                }
                continue;
            }

            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            auto result = it->future.get();
            pendingMemoryReserved -= std::min(it->estimatedMemory, pendingMemoryReserved);

            if (!result.success)
            {
                it = pendingLoads.erase(it);
                continue;
            }

            auto tileIt = tileMap.find(result.key);
            if (tileIt == tileMap.end() || !tileIt->second)
            {
                it = pendingLoads.erase(it);
                continue;
            }

            terrain::TerrainTile* tile = tileIt->second;

            // If tile was dirtied by a brush while we were loading, discard stale data
            if (tile->hasAnyGPUDirtyLOD())
            {
                it = pendingLoads.erase(it);
                continue;
            }

            if (uploadsThisPoll >= config.maxUploadsPerFrame)
            {
                ++it;
                continue;
            }

            // Apply loaded data to tile
            tile->lodLevels = std::move(result.lodData);
            tile->isDirty = false;
            tile->dirtyLODMask = 0;
            if (tile->hasHeightData())
                tile->updateWorldBounds();

            if (result.hasWeightMap)
            {
                tile->weightMap = std::move(result.weightMap);
                tile->weightMapGPUDirty = true;
            }

            if (result.hasHoleMask)
            {
                tile->holeMask = std::move(result.holeMask);
                tile->topologyDirty = true;
            }

            // Upload all available LODs to GPU
            for (uint8_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                if (tile->lodLevels[lod].isEmpty())
                    continue;

                auto infoIt = tileInfos.find(result.key);
                if (infoIt == tileInfos.end())
                    continue;

                if (infoIt->second.hasLODLoaded(lod))
                {
                    // Re-upload (evict old, upload new)
                    evictTileLOD(result.key, lod);
                }

                if (adapter.uploadTileAddLOD(*tile, lod))
                {
                    infoIt->second.setLODLoaded(lod);

                    if (lod < infoIt->second.currentLoadedLOD || infoIt->second.currentLoadedLOD == 255)
                        infoIt->second.currentLoadedLOD = lod;

                    size_t lodMemory = estimateLODMemory(*tile, lod);
                    infoIt->second.gpuMemoryUsage += lodMemory;
                    currentMemoryUsage += lodMemory;
                    stats.uploadsThisFrame++;
                    stats.bytesUploadedThisFrame += lodMemory;
                    uploadsThisPoll++;
                }
            }

            // Update state
            auto infoIt = tileInfos.find(result.key);
            if (infoIt != tileInfos.end())
            {
                if (infoIt->second.loadedLODMask == 0)
                    infoIt->second.state = TerrainTileStreamState::NotLoaded;
                else if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD)
                    infoIt->second.state = TerrainTileStreamState::FullyLoaded;
                else if (infoIt->second.currentLoadedLOD == FALLBACK_LOD)
                    infoIt->second.state = TerrainTileStreamState::FallbackOnly;
                else
                    infoIt->second.state = TerrainTileStreamState::Streaming;
            }

            it = pendingLoads.erase(it);
        }
    }

    void TerrainStreamManager::submitAsyncLoad(const TerrainTileKey& key, size_t memEstimate)
    {
        if (!tileAsyncDataLoader)
            return;

        auto loader = tileAsyncDataLoader; // copy for lambda capture
        auto future = threading::JobSystem::instance().submit(
            [loader, key]() -> TileLODLoadResult {
                return loader(key);
            }, threading::JobPriority::NORMAL);

        pendingLoads.push_back({key, std::move(future), memEstimate, false});
        pendingMemoryReserved += memEstimate;
    }

    bool TerrainStreamManager::hasPendingLoad(const TerrainTileKey& key) const
    {
        for (const auto& pending : pendingLoads)
        {
            if (pending.key == key && !pending.cancelled)
                return true;
        }
        return false;
    }

    void TerrainStreamManager::cancelPendingLoadsForTile(const TerrainTileKey& key)
    {
        for (auto& pending : pendingLoads)
        {
            if (pending.key == key)
                pending.cancelled = true;
        }
    }

}
