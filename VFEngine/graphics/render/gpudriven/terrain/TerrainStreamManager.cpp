#include "TerrainStreamManager.hpp"
#include "TerrainMeshBuffer.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/CaveMeshGenerator.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <chrono>
#include <future>

namespace render::gpudriven
{
    TerrainStreamManager::TerrainStreamManager(TerrainMeshBuffer& terrainBuf,
                                               TerrainGPUAdapter& gpuAdapter)
        : terrainBuffer(terrainBuf), adapter(gpuAdapter) {}

    TerrainStreamManager::~TerrainStreamManager() { clear(); }

    void TerrainStreamManager::update(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                      const glm::vec3& cameraPosition)
    {
        currentFrame++;
        stats.uploadsThisFrame = 0;
        stats.bytesUploadedThisFrame = 0;
        stats.tilesStreaming = 0;

        tileMap_.clear();
        for (auto* tile : visibleTiles)
            if (tile && tile->isVisible)
                tileMap_[{tile->coord.x, tile->coord.z}] = tile;

        pollCompletions();

        std::vector<TileWithDistance> sortedTiles;
        buildSortedTileList(visibleTiles, cameraPosition, sortedTiles);
        updateFallbackLODs(sortedTiles);

        uint32_t fileReadsThisFrame = 0;
        updateGPUDirtyLODs(visibleTiles, fileReadsThisFrame);
        updateWeightMapsAndCaves(visibleTiles);
        updateDetailLODs();
        processEvictions(cameraPosition);
        updateStats();
        tileMap_.clear();
    }

    void TerrainStreamManager::buildSortedTileList(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                                    const glm::vec3& cameraPosition,
                                                    std::vector<TileWithDistance>& outSorted)
    {
        outSorted.reserve(visibleTiles.size());
        for (auto* tile : visibleTiles)
        {
            if (!tile || !tile->isVisible) continue;
            glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
            outSorted.push_back({tile, glm::length(tileCenter - cameraPosition)});
        }
        std::sort(outSorted.begin(), outSorted.end(),
                  [](const TileWithDistance& a, const TileWithDistance& b) { return a.distance < b.distance; });
    }

    void TerrainStreamManager::updateFallbackLODs(const std::vector<TileWithDistance>& sortedTiles)
    {
        uint32_t fallbackUploads = 0;
        size_t fallbackBytes = 0;
        for (const auto& entry : sortedTiles)
        {
            auto* tile = entry.tile;
            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto infoIt = tileInfos.find(key);
            if (infoIt == tileInfos.end())
            {
                TerrainTileStreamInfo info{};
                info.key = key;  info.state = TerrainTileStreamState::NotLoaded;
                info.currentLoadedLOD = 255;  info.distanceToCamera = entry.distance;
                info.targetLOD = selectTargetLOD(entry.distance);  info.lastAccessFrame = currentFrame;
                infoIt = tileInfos.emplace(key, info).first;
            }
            else
            {
                infoIt->second.distanceToCamera = entry.distance;
                infoIt->second.targetLOD = selectTargetLOD(entry.distance);
                infoIt->second.lastAccessFrame = currentFrame;
            }
            if (tile->hasWeightMap() && tile->weightMapGPUDirty)
                if (adapter.uploadWeightMap(*tile)) tile->weightMapGPUDirty = false;

            if (infoIt->second.hasLODLoaded(FALLBACK_LOD)) continue;
            if (fallbackUploads >= config.maxFallbackUploadsPerFrame || fallbackBytes >= config.maxFallbackBytesPerFrame) continue;
            if (tile->lodLevels[FALLBACK_LOD].isEmpty())
            {
                if (tileLoadContextProvider && pendingLoads.size() < maxFileReadsPerFrame && !hasPendingLoad(key))
                    submitAsyncLoad(key, LOD_MEMORY_ESTIMATE[FALLBACK_LOD]);
                continue;
            }
            if (adapter.uploadTileAddLOD(*tile, FALLBACK_LOD))
            {
                infoIt->second.setLODLoaded(FALLBACK_LOD);
                if (infoIt->second.currentLoadedLOD == 255) infoIt->second.currentLoadedLOD = FALLBACK_LOD;
                infoIt->second.state = TerrainTileStreamState::FallbackOnly;
                size_t lodMemory = estimateLODMemory(*tile, FALLBACK_LOD);
                infoIt->second.gpuMemoryUsage += lodMemory;
                currentMemoryUsage += lodMemory;
                stats.uploadsThisFrame++;  stats.bytesUploadedThisFrame += lodMemory;
                fallbackUploads++;  fallbackBytes += lodMemory;
            }
        }
    }

    void TerrainStreamManager::updateGPUDirtyLODs(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                                   uint32_t& fileReadsThisFrame)
    {
        for (auto* tile : visibleTiles)
        {
            if (!tile || !tile->hasAnyGPUDirtyLOD()) continue;
            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto infoIt = tileInfos.find(key);

            for (uint8_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                if (!tile->isLODGPUDirty(lod)) continue;
                if (infoIt != tileInfos.end() && infoIt->second.hasLODLoaded(lod))
                {
                    if (tile->lodLevels[lod].isEmpty())
                    {
                        if (!tileDataLoader || fileReadsThisFrame >= maxFileReadsPerFrame) continue;
                        if (!tileDataLoader(*tile, lod)) continue;
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
    }

    void TerrainStreamManager::updateWeightMapsAndCaves(const std::vector<terrain::TerrainTile*>& visibleTiles)
    {
        for (auto* tile : visibleTiles)
        {
            if (!tile || !tile->weightMapGPUDirty || !tile->hasWeightMap()) continue;
            if (adapter.uploadWeightMap(*tile))
                tile->weightMapGPUDirty = false;
        }
        for (auto* tile : visibleTiles)
        {
            if (!tile || !tile->caveGPUDirty) continue;
            if (tile->hasCaveData() && tile->caveData->hasCaveGeometry() && tile->caveLOD.isEmpty())
                terrain::CaveMeshGenerator::generate(*tile);
            if (!tile->hasCaveGeometry() || tile->caveLOD.isEmpty()) { tile->caveGPUDirty = false; continue; }
            if (adapter.uploadCaveMesh(*tile))
                tile->caveGPUDirty = false;
        }
    }

    void TerrainStreamManager::updateDetailLODs()
    {
        while (!uploadQueue.empty()) uploadQueue.pop();
        for (auto& [key, info] : tileInfos)
        {
            if (info.lastAccessFrame != currentFrame) continue;
            for (uint8_t lod = 0; lod < FALLBACK_LOD; ++lod)
                if (lod <= info.targetLOD && !info.hasLODLoaded(lod))
                    uploadQueue.push({key, lod, calculatePriority(info.distanceToCamera, lod, info.currentLoadedLOD)});
        }
        size_t bytesUploaded = 0;  uint32_t uploadsCount = 0;
        while (!uploadQueue.empty() && uploadsCount < config.maxUploadsPerFrame && bytesUploaded < config.maxBytesPerFrame)
        {
            size_t budgetUsed = currentMemoryUsage + pendingMemoryReserved;
            if (budgetUsed >= static_cast<size_t>(config.memoryBudgetBytes * config.evictionThreshold)) break;
            StreamPriorityEntry entry = uploadQueue.top();  uploadQueue.pop();
            auto tileIt = tileMap_.find(entry.key);
            if (tileIt == tileMap_.end() || !tileIt->second) continue;
            auto* tile = tileIt->second;
            auto infoIt = tileInfos.find(entry.key);
            if (infoIt == tileInfos.end() || infoIt->second.hasLODLoaded(entry.targetLOD)) continue;
            size_t lodMemory = estimateLODMemory(*tile, entry.targetLOD);
            if (currentMemoryUsage + lodMemory > config.memoryBudgetBytes) continue;
            if (tile->lodLevels[entry.targetLOD].isEmpty())
            {
                if (tileLoadContextProvider && pendingLoads.size() < maxFileReadsPerFrame && !hasPendingLoad(entry.key))
                    submitAsyncLoad(entry.key, lodMemory);
                continue;
            }
            if (adapter.uploadTileAddLOD(*tile, entry.targetLOD))
            {
                infoIt->second.setLODLoaded(entry.targetLOD);
                if (entry.targetLOD < infoIt->second.currentLoadedLOD) infoIt->second.currentLoadedLOD = entry.targetLOD;
                if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD)
                    infoIt->second.state = TerrainTileStreamState::FullyLoaded;
                else { infoIt->second.state = TerrainTileStreamState::Streaming; stats.tilesStreaming++; }
                infoIt->second.gpuMemoryUsage += lodMemory;  currentMemoryUsage += lodMemory;
                bytesUploaded += lodMemory;  uploadsCount++;
                stats.uploadsThisFrame++;  stats.bytesUploadedThisFrame += lodMemory;
            }
        }
    }

    void TerrainStreamManager::updateStats()
    {
        stats.memoryUsedBytes = currentMemoryUsage;
        stats.memoryBudgetBytes = config.memoryBudgetBytes;
        stats.tilesLoaded = static_cast<uint32_t>(tileInfos.size());
        stats.fallbackTiles = 0;
        stats.fullDetailTiles = 0;
        for (const auto& [key, info] : tileInfos)
        {
            if (info.loadedLODMask == 0) continue;
            if (info.currentLoadedLOD == 0) stats.fullDetailTiles++;
            else if (info.currentLoadedLOD == TERRAIN_LOD_LEVEL_COUNT - 1) stats.fallbackTiles++;
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

    float TerrainStreamManager::calculatePriority(float distance, uint8_t targetLOD, uint8_t currentLOD) const
    {
        float distancePriority = 1.0f / (1.0f + distance * 0.01f);
        float lodUrgency = 1.0f;
        if (currentLOD != 255 && targetLOD < currentLOD)
            lodUrgency = 2.0f + (currentLOD - targetLOD) * 0.5f;
        else if (currentLOD == 255)
            lodUrgency = 3.0f;
        return distancePriority * lodUrgency;
    }

    void TerrainStreamManager::buildEvictionCandidates(std::vector<EvictionCandidate>& candidates)
    {
        for (auto& [key, info] : tileInfos)
        {
            if (info.lastAccessFrame == currentFrame) continue;
            uint8_t maxLOD = config.keepFallbackLoaded ? FALLBACK_LOD : TERRAIN_LOD_LEVEL_COUNT;
            for (uint8_t lod = 0; lod < maxLOD; ++lod)
            {
                if (!info.hasLODLoaded(lod)) continue;
                uint64_t age = currentFrame - info.lastAccessFrame;
                float evictionScore = info.distanceToCamera * static_cast<float>(age) * (1.0f + lod * 0.25f);
                candidates.push_back({key, lod, evictionScore, LOD_MEMORY_ESTIMATE[lod]});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const EvictionCandidate& a, const EvictionCandidate& b) { return a.evictionScore > b.evictionScore; });
    }

    void TerrainStreamManager::processEvictions(const glm::vec3& cameraPosition)
    {
        if (currentMemoryUsage < config.memoryBudgetBytes * config.evictionThreshold) return;

        std::vector<EvictionCandidate> candidates;
        buildEvictionCandidates(candidates);
        size_t targetMemory = static_cast<size_t>(config.memoryBudgetBytes * 0.8f);

        for (const auto& candidate : candidates)
        {
            if (currentMemoryUsage <= targetMemory) break;
            evictTileLOD(candidate.key, candidate.lodLevel);
            cancelPendingLoadsForTile(candidate.key);
            if (tileRAMEvictor)
            {
                auto infoIt = tileInfos.find(candidate.key);
                if (infoIt != tileInfos.end() && infoIt->second.loadedLODMask == 0)
                {
                    auto tileIt = tileMap_.find(candidate.key);
                    if (tileIt != tileMap_.end() && tileIt->second)
                        tileRAMEvictor(*tileIt->second);
                }
            }
        }
    }

    void TerrainStreamManager::evictTileLOD(const TerrainTileKey& key, uint8_t lodLevel)
    {
        auto infoIt = tileInfos.find(key);
        if (infoIt == tileInfos.end()) return;

        auto& info = infoIt->second;
        if (!info.hasLODLoaded(lodLevel)) return;

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
                if (info.hasLODLoaded(lod)) { info.currentLoadedLOD = lod; break; }
            info.state = (info.currentLoadedLOD == FALLBACK_LOD)
                ? TerrainTileStreamState::FallbackOnly : TerrainTileStreamState::Streaming;
        }
    }

    size_t TerrainStreamManager::estimateLODMemory(const terrain::TerrainTile& tile, uint8_t lodLevel) const
    {
        if (lodLevel >= TERRAIN_LOD_LEVEL_COUNT) return 0;
        const auto& lodData = tile.lodLevels[lodLevel];
        if (lodData.isEmpty()) return LOD_MEMORY_ESTIMATE[lodLevel];
        return lodData.vertices.size() * sizeof(resource::Vertex)
             + lodData.indices.size() * sizeof(uint32_t)
             + lodData.meshlets.size() * sizeof(GPUMeshlet)
             + lodData.meshletVertices.size() * sizeof(uint32_t)
             + lodData.meshletPrimitives.size() * sizeof(uint32_t);
    }

    void TerrainStreamManager::clear()
    {
        adapter.clear();
        tileInfos.clear();
        while (!uploadQueue.empty()) uploadQueue.pop();
        pendingLoads.clear();
        pendingLoadKeys.clear();
        pendingMemoryReserved = 0;
        currentMemoryUsage = 0;
        stats = TerrainStreamingStats{};
    }

    void TerrainStreamManager::evictTile(int32_t coordX, int32_t coordZ)
    {
        TerrainTileKey key{coordX, coordZ};
        cancelPendingLoadsForTile(key);
        auto infoIt = tileInfos.find(key);
        if (infoIt == tileInfos.end()) return;
        for (uint8_t lod = 0; lod < 4; ++lod)
            if (infoIt->second.hasLODLoaded(lod))
                evictTileLOD(key, lod);
        tileInfos.erase(infoIt);
    }

    void TerrainStreamManager::pollCompletions()
    {
        uint32_t uploadsThisPoll = 0;
        auto it = pendingLoads.begin();
        while (it != pendingLoads.end())
        {
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            auto result = it->future.get();
            pendingMemoryReserved -= std::min(it->estimatedMemory, pendingMemoryReserved);
            pendingLoadKeys.erase(it->key);

            if (!result.success)
            {
                it = pendingLoads.erase(it);
                continue;
            }

            auto tileIt = tileMap_.find(result.key);
            if (tileIt == tileMap_.end() || !tileIt->second)
            {
                it = pendingLoads.erase(it);
                continue;
            }

            auto* tile = tileIt->second;
            if (tile->hasAnyGPUDirtyLOD() || uploadsThisPoll >= config.maxUploadsPerFrame)
            {
                it = pendingLoads.erase(it);
                continue;
            }

            tile->lodLevels = std::move(result.lodData);
            tile->isDirty = false;
            tile->dirtyLODMask = 0;
            if (tile->hasHeightData()) tile->updateWorldBounds();
            if (result.hasWeightMap) { tile->weightMap = std::move(result.weightMap); tile->weightMapGPUDirty = true; }
            if (result.hasHoleMask) { tile->holeMask = std::move(result.holeMask); tile->topologyDirty = true; }

            for (uint8_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                if (tile->lodLevels[lod].isEmpty()) continue;
                auto infoIt = tileInfos.find(result.key);
                if (infoIt == tileInfos.end()) continue;
                if (infoIt->second.hasLODLoaded(lod))
                    evictTileLOD(result.key, lod);
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

            auto infoIt = tileInfos.find(result.key);
            if (infoIt != tileInfos.end())
            {
                if (infoIt->second.loadedLODMask == 0) infoIt->second.state = TerrainTileStreamState::NotLoaded;
                else if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD) infoIt->second.state = TerrainTileStreamState::FullyLoaded;
                else if (infoIt->second.currentLoadedLOD == FALLBACK_LOD) infoIt->second.state = TerrainTileStreamState::FallbackOnly;
                else infoIt->second.state = TerrainTileStreamState::Streaming;
            }

            it = pendingLoads.erase(it);
        }
    }

    void TerrainStreamManager::submitAsyncLoad(const TerrainTileKey& key, size_t memEstimate)
    {
        if (!tileLoadContextProvider) return;

        // Phase 1 (main thread): gather file path + index entry — no I/O, just map lookups
        auto ctx = tileLoadContextProvider(key);
        if (!ctx.valid) return;

        // Phase 2 (worker thread): perform actual file I/O
        auto future = std::async(std::launch::async, [ctx = std::move(ctx)]() -> TileLODLoadResult {
            TileLODLoadResult result;
            result.key = ctx.key;

            if (!ctx.hasMeshletCache || ctx.indexEntry.meshletDataOffset == 0)
                return result;

            if (terrain::TerrainSerializer::readTileLODData(ctx.filePath, ctx.indexEntry, result.lodData))
            {
                result.success = true;

                if (ctx.indexEntry.weightDataOffset != 0)
                    if (terrain::TerrainSerializer::readTileWeights(ctx.filePath, ctx.indexEntry, result.weightMap))
                        result.hasWeightMap = true;

                if (ctx.indexEntry.holeMaskDataOffset != 0)
                    if (terrain::TerrainSerializer::readTileHoleMask(ctx.filePath, ctx.indexEntry, result.holeMask))
                        result.hasHoleMask = true;
            }

            return result;
        });

        pendingLoads.push_back({key, std::move(future), memEstimate});
        pendingLoadKeys.insert(key);
        pendingMemoryReserved += memEstimate;
    }

    bool TerrainStreamManager::hasPendingLoad(const TerrainTileKey& key) const
    {
        return pendingLoadKeys.count(key) > 0;
    }

    void TerrainStreamManager::cancelPendingLoadsForTile(const TerrainTileKey& key)
    {
        // std::async futures block on destruction, so just let them complete naturally.
        // Mark for removal by erasing from the key set — pollCompletions will discard
        // the result when the tile is no longer in tileMap_.
        pendingLoadKeys.erase(key);
    }

}
