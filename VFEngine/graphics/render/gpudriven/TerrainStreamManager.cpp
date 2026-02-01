#include "TerrainStreamManager.hpp"
#include "TerrainMeshBuffer.hpp"
#include "terrain/TerrainTile.hpp"
#include "print/EditorLogger.hpp"
#include <algorithm>

namespace render::gpudriven
{
    TerrainStreamManager::TerrainStreamManager(TerrainMeshBuffer& terrainBuffer,
                                               TerrainGPUAdapter& adapter)
        : terrainBuffer_(terrainBuffer)
        , adapter_(adapter)
    {
    }

    TerrainStreamManager::~TerrainStreamManager()
    {
        clear();
    }

    void TerrainStreamManager::update(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                      const glm::vec3& cameraPosition)
    {
        currentFrame_++;

        // Reset per-frame stats
        stats_.uploadsThisFrame = 0;
        stats_.bytesUploadedThisFrame = 0;
        stats_.tilesStreaming = 0;

        // Build tile map for quick lookup
        std::unordered_map<TerrainTileKey, terrain::TerrainTile*, TerrainTileKeyHash> tileMap;
        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (tile && tile->isVisible)
            {
                tileMap[{tile->coord.x, tile->coord.z}] = tile;
            }
        }

        // ============================================
        // PHASE 1: Guaranteed fallback (LOD3) for all visible tiles
        // No bandwidth limit - ensures tiles always render
        // ============================================
        for (terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->isVisible)
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
            float distance = glm::length(tileCenter - cameraPosition);

            // Get or create tile info
            auto infoIt = tileInfos_.find(key);
            if (infoIt == tileInfos_.end())
            {
                TerrainTileStreamInfo info;
                info.key = key;
                info.state = TerrainTileStreamState::NotLoaded;
                info.currentLoadedLOD = 255;
                info.distanceToCamera = distance;
                info.targetLOD = selectTargetLOD(distance);
                info.lastAccessFrame = currentFrame_;
                auto result = tileInfos_.emplace(key, info);
                infoIt = result.first;
            }
            else
            {
                // Update tracking for existing tiles
                infoIt->second.distanceToCamera = distance;
                infoIt->second.targetLOD = selectTargetLOD(distance);
                infoIt->second.lastAccessFrame = currentFrame_;
            }

            // Upload LOD3 (fallback) if not already loaded
            if (!infoIt->second.hasLODLoaded(3))
            {
                if (adapter_.uploadTileAddLOD(*tile, 3))
                {
                    infoIt->second.setLODLoaded(3);
                    if (infoIt->second.currentLoadedLOD == 255)
                    {
                        infoIt->second.currentLoadedLOD = 3;
                    }
                    infoIt->second.state = TerrainTileStreamState::FallbackOnly;

                    size_t lodMemory = estimateLODMemory(*tile, 3);
                    infoIt->second.gpuMemoryUsage += lodMemory;
                    currentMemoryUsage_ += lodMemory;
                    stats_.uploadsThisFrame++;
                    stats_.bytesUploadedThisFrame += lodMemory;
                }
            }
        }

        // ============================================
        // PHASE 2: Stream higher detail LODs (0-2) with bandwidth limits
        // Priority-based streaming for LOD upgrades
        // ============================================

        // Build priority queue for LOD 0-2 upgrades
        while (!uploadQueue_.empty())
        {
            uploadQueue_.pop();
        }

        for (auto& [key, info] : tileInfos_)
        {
            // Skip if not accessed this frame (not visible)
            if (info.lastAccessFrame != currentFrame_)
                continue;

            // Queue LOD 0-2 upgrades based on target LOD
            uint8_t targetLOD = info.targetLOD;

            // Only queue if we need a better LOD than what we have
            for (uint8_t lod = 0; lod < 3; ++lod)
            {
                if (lod <= targetLOD && !info.hasLODLoaded(lod))
                {
                    float priority = calculatePriority(info.distanceToCamera, lod, info.currentLoadedLOD);
                    uploadQueue_.push({key, lod, priority});
                }
            }
        }

        // Process upload queue with bandwidth limits
        size_t bytesUploaded = 0;
        uint32_t uploadsCount = 0;

        while (!uploadQueue_.empty() &&
               uploadsCount < config_.maxUploadsPerFrame &&
               bytesUploaded < config_.maxBytesPerFrame)
        {
            // Check memory budget
            if (currentMemoryUsage_ >= config_.memoryBudgetBytes * config_.evictionThreshold)
            {
                break;
            }

            StreamPriorityEntry entry = uploadQueue_.top();
            uploadQueue_.pop();

            // Find the tile
            auto tileIt = tileMap.find(entry.key);
            if (tileIt == tileMap.end())
                continue;

            terrain::TerrainTile* tile = tileIt->second;
            if (!tile)
                continue;

            // Check if LOD is already loaded
            auto infoIt = tileInfos_.find(entry.key);
            if (infoIt == tileInfos_.end() || infoIt->second.hasLODLoaded(entry.targetLOD))
                continue;

            // Estimate memory for this LOD
            size_t lodMemory = estimateLODMemory(*tile, entry.targetLOD);

            // Check memory budget
            if (currentMemoryUsage_ + lodMemory > config_.memoryBudgetBytes)
                continue;

            // Upload the LOD
            if (adapter_.uploadTileAddLOD(*tile, entry.targetLOD))
            {
                infoIt->second.setLODLoaded(entry.targetLOD);

                // Update current loaded LOD to finest available
                if (entry.targetLOD < infoIt->second.currentLoadedLOD)
                {
                    infoIt->second.currentLoadedLOD = entry.targetLOD;
                }

                // Update state
                if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD)
                {
                    infoIt->second.state = TerrainTileStreamState::FullyLoaded;
                }
                else
                {
                    infoIt->second.state = TerrainTileStreamState::Streaming;
                    stats_.tilesStreaming++;
                }

                infoIt->second.gpuMemoryUsage += lodMemory;
                currentMemoryUsage_ += lodMemory;
                bytesUploaded += lodMemory;
                uploadsCount++;
                stats_.uploadsThisFrame++;
                stats_.bytesUploadedThisFrame += lodMemory;
            }
        }

        // ============================================
        // PHASE 3: Eviction of unused tiles/LODs
        // ============================================
        processEvictions(cameraPosition);

        // Update statistics
        stats_.memoryUsedBytes = currentMemoryUsage_;
        stats_.memoryBudgetBytes = config_.memoryBudgetBytes;
        stats_.tilesLoaded = static_cast<uint32_t>(tileInfos_.size());

        // Count tiles by LOD quality
        stats_.fallbackTiles = 0;
        stats_.fullDetailTiles = 0;
        for (const auto& [key, info] : tileInfos_)
        {
            if (info.loadedLODMask == 0)
                continue;

            if (info.currentLoadedLOD == 0)
                stats_.fullDetailTiles++;
            else if (info.currentLoadedLOD == 3)
                stats_.fallbackTiles++;
        }
    }

    void TerrainStreamManager::updateTileInfos(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                               const glm::vec3& cameraPosition)
    {
        for (const terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->isVisible)
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};

            // Calculate distance to camera
            glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
            float distance = glm::length(tileCenter - cameraPosition);

            // Determine target LOD based on distance
            uint8_t targetLOD = selectTargetLOD(distance);

            // Get or create tile info
            auto it = tileInfos_.find(key);
            if (it == tileInfos_.end())
            {
                TerrainTileStreamInfo info;
                info.key = key;
                info.state = TerrainTileStreamState::NotLoaded;
                info.currentLoadedLOD = 255;
                info.targetLOD = targetLOD;
                info.distanceToCamera = distance;
                info.lastAccessFrame = currentFrame_;
                auto result = tileInfos_.emplace(key, info);
                it = result.first;
            }
            else
            {
                it->second.targetLOD = targetLOD;
                it->second.distanceToCamera = distance;
                it->second.lastAccessFrame = currentFrame_;
            }

            // Update state based on loaded LODs
            auto& info = it->second;
            if (info.loadedLODMask == 0)
            {
                info.state = TerrainTileStreamState::NotLoaded;
            }
            else if (info.hasLODLoaded(info.targetLOD))
            {
                info.state = TerrainTileStreamState::FullyLoaded;
                info.currentLoadedLOD = info.targetLOD;
            }
            else if (info.hasLODLoaded(3))
            {
                info.state = TerrainTileStreamState::FallbackOnly;
                // Find the best loaded LOD
                for (uint8_t lod = 0; lod < 4; ++lod)
                {
                    if (info.hasLODLoaded(lod))
                    {
                        info.currentLoadedLOD = lod;
                        break;
                    }
                }
            }
        }
    }

    uint8_t TerrainStreamManager::selectTargetLOD(float distance) const
    {
        // LOD selection based on distance thresholds
        // These should be configurable, using reasonable defaults
        if (distance < 50.0f)  return 0;  // Ultra close: highest detail
        if (distance < 150.0f) return 1;  // Close
        if (distance < 300.0f) return 2;  // Medium
        return 3;                          // Far: lowest detail
    }

    void TerrainStreamManager::buildUploadQueue(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                                const glm::vec3& cameraPosition)
    {
        // Clear the queue
        while (!uploadQueue_.empty())
        {
            uploadQueue_.pop();
        }

        for (const terrain::TerrainTile* tile : visibleTiles)
        {
            if (!tile || !tile->isVisible)
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto it = tileInfos_.find(key);
            if (it == tileInfos_.end())
                continue;

            const auto& info = it->second;

            // If tile needs to load fallback (LOD3) first
            if (config_.keepFallbackLoaded && !info.hasLODLoaded(3))
            {
                float priority = calculatePriority(*tile, cameraPosition, 3, 255);
                uploadQueue_.push({key, 3, priority * 2.0f}); // Boost fallback priority
            }

            // If tile needs to upgrade to target LOD
            if (!info.hasLODLoaded(info.targetLOD))
            {
                float priority = calculatePriority(*tile, cameraPosition, info.targetLOD, info.currentLoadedLOD);
                uploadQueue_.push({key, info.targetLOD, priority});
            }
        }
    }

    float TerrainStreamManager::calculatePriority(const terrain::TerrainTile& tile,
                                                  const glm::vec3& cameraPosition,
                                                  uint8_t targetLOD,
                                                  uint8_t currentLOD) const
    {
        glm::vec3 tileCenter = (tile.worldBounds.min + tile.worldBounds.max) * 0.5f;
        float distance = glm::length(tileCenter - cameraPosition);
        return calculatePriority(distance, targetLOD, currentLOD);
    }

    float TerrainStreamManager::calculatePriority(float distance,
                                                  uint8_t targetLOD,
                                                  uint8_t currentLOD) const
    {
        // Higher priority for closer tiles
        float distancePriority = 1.0f / (1.0f + distance * 0.01f);

        // Urgency boost if need to upgrade LOD (current is worse than target)
        float lodUrgency = 1.0f;
        if (currentLOD != 255 && targetLOD < currentLOD)
        {
            lodUrgency = 2.0f + (currentLOD - targetLOD) * 0.5f;
        }
        else if (currentLOD == 255)
        {
            // Nothing loaded - high urgency
            lodUrgency = 3.0f;
        }

        // Visibility is implied since we only process visible tiles

        return distancePriority * lodUrgency;
    }

    void TerrainStreamManager::processUploadQueue(const std::vector<terrain::TerrainTile*>& visibleTiles)
    {
        // Build a map for quick tile lookup
        std::unordered_map<TerrainTileKey, const terrain::TerrainTile*, TerrainTileKeyHash> tileMap;
        for (const terrain::TerrainTile* tile : visibleTiles)
        {
            if (tile)
            {
                tileMap[{tile->coord.x, tile->coord.z}] = tile;
            }
        }

        size_t bytesUploaded = 0;
        uint32_t uploadsCount = 0;

        while (!uploadQueue_.empty() &&
               uploadsCount < config_.maxUploadsPerFrame &&
               bytesUploaded < config_.maxBytesPerFrame)
        {
            // Check memory budget before uploading
            if (currentMemoryUsage_ >= config_.memoryBudgetBytes * config_.evictionThreshold)
            {
                // At or near budget limit - stop uploading until evictions make space
                break;
            }

            StreamPriorityEntry entry = uploadQueue_.top();
            uploadQueue_.pop();

            // Find the tile
            auto tileIt = tileMap.find(entry.key);
            if (tileIt == tileMap.end())
                continue;

            const terrain::TerrainTile* tile = tileIt->second;
            if (!tile)
                continue;

            // Check if this LOD is already loaded
            auto infoIt = tileInfos_.find(entry.key);
            if (infoIt != tileInfos_.end() && infoIt->second.hasLODLoaded(entry.targetLOD))
                continue;

            // Estimate memory for this LOD
            size_t lodMemory = estimateLODMemory(*tile, entry.targetLOD);

            // Check if we have budget for this upload
            if (currentMemoryUsage_ + lodMemory > config_.memoryBudgetBytes)
            {
                // Can't fit this one, try next
                continue;
            }

            // Upload the LOD
            if (uploadTileLOD(*tile, entry.targetLOD))
            {
                bytesUploaded += lodMemory;
                uploadsCount++;
                stats_.uploadsThisFrame++;
                stats_.bytesUploadedThisFrame += lodMemory;
            }
        }
    }

    void TerrainStreamManager::processEvictions(const glm::vec3& cameraPosition)
    {
        // Only evict if we're approaching the budget limit
        if (currentMemoryUsage_ < config_.memoryBudgetBytes * config_.evictionThreshold)
        {
            return;
        }

        // Build list of eviction candidates (tiles with high LOD loaded that aren't close)
        struct EvictionCandidate
        {
            TerrainTileKey key;
            uint8_t lodLevel;
            float evictionScore; // Higher = more likely to evict
            size_t memorySize;
        };

        std::vector<EvictionCandidate> candidates;

        for (auto& [key, info] : tileInfos_)
        {
            // Skip tiles that were accessed this frame (currently visible)
            if (info.lastAccessFrame == currentFrame_)
                continue;

            // Check each loaded LOD (except LOD3 if keepFallbackLoaded is true)
            for (uint8_t lod = 0; lod < (config_.keepFallbackLoaded ? 3 : 4); ++lod)
            {
                if (!info.hasLODLoaded(lod))
                    continue;

                // Calculate eviction score: distance * age
                uint64_t age = currentFrame_ - info.lastAccessFrame;
                float evictionScore = info.distanceToCamera * static_cast<float>(age);

                // Higher LOD (lower detail) is more evictable
                evictionScore *= (1.0f + lod * 0.25f);

                candidates.push_back({key, lod, evictionScore, LOD_MEMORY_ESTIMATE[lod]});
            }
        }

        // Sort by eviction score (highest first - most evictable)
        std::sort(candidates.begin(), candidates.end(),
                  [](const EvictionCandidate& a, const EvictionCandidate& b) {
                      return a.evictionScore > b.evictionScore;
                  });

        // Evict until we're under threshold
        size_t targetMemory = static_cast<size_t>(config_.memoryBudgetBytes * 0.8f); // Target 80% usage

        for (const auto& candidate : candidates)
        {
            if (currentMemoryUsage_ <= targetMemory)
                break;

            evictTileLOD(candidate.key, candidate.lodLevel);
            stats_.tilesEvicted++;
        }
    }

    bool TerrainStreamManager::uploadTileLOD(const terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        if (lodLevel >= 4)
            return false;

        TerrainTileKey key{tile.coord.x, tile.coord.z};

        // Check if this specific LOD is already uploaded
        auto infoIt = tileInfos_.find(key);
        if (infoIt != tileInfos_.end() && infoIt->second.hasLODLoaded(lodLevel))
        {
            return true; // Already loaded
        }

        // Upload the specific LOD using per-LOD upload
        if (!adapter_.uploadTileAddLOD(tile, lodLevel))
        {
            return false;
        }

        // Calculate memory for this LOD
        size_t lodMemory = estimateLODMemory(tile, lodLevel);

        if (infoIt == tileInfos_.end())
        {
            TerrainTileStreamInfo info;
            info.key = key;
            info.setLODLoaded(lodLevel);
            info.currentLoadedLOD = lodLevel;
            info.state = (lodLevel == 3) ? TerrainTileStreamState::FallbackOnly :
                         TerrainTileStreamState::Streaming;
            info.lastAccessFrame = currentFrame_;
            info.gpuMemoryUsage = lodMemory;
            currentMemoryUsage_ += lodMemory;

            tileInfos_.emplace(key, info);
        }
        else
        {
            infoIt->second.setLODLoaded(lodLevel);
            if (lodLevel < infoIt->second.currentLoadedLOD)
            {
                infoIt->second.currentLoadedLOD = lodLevel;
            }
            infoIt->second.gpuMemoryUsage += lodMemory;
            currentMemoryUsage_ += lodMemory;

            // Update state
            if (infoIt->second.currentLoadedLOD == infoIt->second.targetLOD)
            {
                infoIt->second.state = TerrainTileStreamState::FullyLoaded;
            }
            else if (infoIt->second.currentLoadedLOD == 3)
            {
                infoIt->second.state = TerrainTileStreamState::FallbackOnly;
            }
            else
            {
                infoIt->second.state = TerrainTileStreamState::Streaming;
            }
        }

        return true;
    }

    void TerrainStreamManager::evictTileLOD(const TerrainTileKey& key, uint8_t lodLevel)
    {
        auto infoIt = tileInfos_.find(key);
        if (infoIt == tileInfos_.end())
            return;

        auto& info = infoIt->second;
        if (!info.hasLODLoaded(lodLevel))
            return;

        // Use adapter's per-LOD removal
        adapter_.removeTileLOD(key, lodLevel);

        size_t lodMemory = LOD_MEMORY_ESTIMATE[lodLevel];
        currentMemoryUsage_ -= std::min(lodMemory, info.gpuMemoryUsage);
        info.gpuMemoryUsage -= std::min(lodMemory, info.gpuMemoryUsage);
        info.clearLODLoaded(lodLevel);

        if (info.loadedLODMask == 0)
        {
            // No LODs remaining - mark as not loaded
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

            // Update state
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

        // Calculate actual memory based on geometry
        size_t vertexMemory = lodData.vertices.size() * sizeof(resource::Vertex);
        size_t indexMemory = lodData.indices.size() * sizeof(uint32_t);
        size_t meshletMemory = lodData.meshlets.size() * sizeof(GPUMeshlet);
        size_t meshletVertexMemory = lodData.meshletVertices.size() * sizeof(uint32_t);
        size_t meshletPrimitiveMemory = lodData.meshletPrimitives.size() * sizeof(uint32_t);

        return vertexMemory + indexMemory + meshletMemory + meshletVertexMemory + meshletPrimitiveMemory;
    }

    const TerrainTileStreamInfo* TerrainStreamManager::getTileInfo(const TerrainTileKey& key) const
    {
        auto it = tileInfos_.find(key);
        return (it != tileInfos_.end()) ? &it->second : nullptr;
    }

    uint8_t TerrainStreamManager::getBestAvailableLOD(const TerrainTileKey& key) const
    {
        auto it = tileInfos_.find(key);
        if (it == tileInfos_.end())
            return 255; // Not loaded

        const auto& info = it->second;
        // Find the finest LOD that's loaded
        for (uint8_t lod = 0; lod < 4; ++lod)
        {
            if (info.hasLODLoaded(lod))
                return lod;
        }

        return 255; // Nothing loaded
    }

    bool TerrainStreamManager::isTileLoaded(const TerrainTileKey& key) const
    {
        auto it = tileInfos_.find(key);
        return it != tileInfos_.end() && it->second.loadedLODMask != 0;
    }

    void TerrainStreamManager::clear()
    {
        // Clear all tiles via adapter
        adapter_.clear();

        tileInfos_.clear();
        while (!uploadQueue_.empty())
        {
            uploadQueue_.pop();
        }

        currentMemoryUsage_ = 0;
        stats_ = TerrainStreamingStats{};
    }

    bool TerrainStreamManager::forceLoadTile(const terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        // Bypass queue and load immediately
        return uploadTileLOD(tile, lodLevel);
    }
}
