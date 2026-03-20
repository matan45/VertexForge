#include "SVTStreamManager.hpp"
#include "PhysicalTileCache.hpp"
#include "SVTPageTable.hpp"
#include "print/Log.hpp"
#include <future>
#include <cstring>

namespace render::svt
{
    SVTStreamManager::SVTStreamManager(PhysicalTileCache& cache, SVTPageTable& pageTable)
        : cache(cache), pageTable(pageTable)
    {
    }

    void SVTStreamManager::init(const SVTConfig& config)
    {
        this->config = config;
        initialized = true;
    }

    void SVTStreamManager::update(const uint32_t* feedbackData, uint32_t entryCount,
                                   uint64_t frameIndex, const glm::vec3& cameraPos)
    {
        if (!initialized || !feedbackData) return;
        currentFrame = frameIndex;

        stats.tilesUploadedThisFrame = 0;
        stats.tilesEvictedThisFrame = 0;
        stats.bytesUploadedThisFrame = 0;

        processFeedback(feedbackData, entryCount, frameIndex, cameraPos);
        touchVisibleTiles(feedbackData, entryCount, frameIndex);
        processEvictions();
        processUploads();

        stats.totalPhysicalTiles = cache.getTileCount();
        stats.usedPhysicalTiles = cache.getTileCount() - cache.getFreeTileCount();
        stats.pendingRequests = static_cast<uint32_t>(requestQueue.size());
    }

    void SVTStreamManager::processFeedback(const uint32_t* feedbackData, uint32_t entryCount,
                                            uint64_t frameIndex, const glm::vec3& cameraPos)
    {
        // Scan feedback buffer for requested tiles
        uint32_t mipLevels = computeMipLevelCount(config.virtualTextureSizeLog2, config.tileSizeLog2);

        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            uint32_t mipOffset = computePageTableMipOffset(mip, config.virtualTextureSizeLog2, config.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(mip, config.virtualTextureSizeLog2, config.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;

            for (uint32_t y = 0; y < tilesPerSide; ++y)
            {
                for (uint32_t x = 0; x < tilesPerSide; ++x)
                {
                    uint32_t idx = mipOffset + y * tilesPerSide + x;
                    if (idx >= entryCount) continue;

                    uint32_t fb = feedbackData[idx];
                    if (!(fb & SVT_FEEDBACK_REQUEST_FLAG)) continue;

                    VirtualTileCoord coord{x, y, mip};

                    // Skip if already resident
                    if (isTileResident(coord)) continue;

                    uint32_t requestedMip = fb & 0xFu;
                    float priority = calculatePriority(coord, requestedMip);

                    requestQueue.push({coord, priority});
                }
            }
        }

        // Process top-priority requests up to budget
        uint32_t tilesGenerated = 0;
        while (!requestQueue.empty() && tilesGenerated < config.maxTilesPerFrame)
        {
            auto req = requestQueue.top();
            requestQueue.pop();

            // Double-check not already resident (may have been queued multiple times)
            if (isTileResident(req.coord)) continue;

            generateTileAsync(req.coord);
            ++tilesGenerated;
        }
    }

    uint32_t SVTStreamManager::allocateOrEvictTile()
    {
        uint32_t physTile = cache.allocateTile();
        if (physTile != SVT_INVALID_TILE) return physTile;

        // Try eviction
        physTile = cache.evictLRU(currentFrame);
        if (physTile == SVT_INVALID_TILE)
        {
            vfLogWarning("SVT: Cache full, cannot upload tile");
            return SVT_INVALID_TILE;
        }

        // Remove evicted tile from resident map
        auto evictedInfo = cache.getTileInfo(physTile);
        if (evictedInfo.occupied)
        {
            residentTiles.erase(evictedInfo.virtualCoord);
            pageTable.clearEntry(evictedInfo.virtualCoord);
            ++stats.tilesEvictedThisFrame;
        }

        return physTile;
    }

    void SVTStreamManager::uploadTileChannels(uint32_t physTile, const SVTTileData& tileData)
    {
        const std::pair<const std::vector<uint8_t>&, uint32_t> channels[] = {
            {tileData.albedoData,   SVT_CHANNEL_ALBEDO},
            {tileData.normalData,   SVT_CHANNEL_NORMAL},
            {tileData.ormData,      SVT_CHANNEL_ORM},
            {tileData.emissionData, SVT_CHANNEL_EMISSION},
            {tileData.heightData,   SVT_CHANNEL_HEIGHT}
        };

        for (auto& [data, channel] : channels)
        {
            if (!data.empty())
                cache.uploadTileData(physTile, channel, data.data(), static_cast<uint32_t>(data.size()));
        }
    }

    void SVTStreamManager::processUploads()
    {
        std::lock_guard<std::mutex> lock(uploadQueueMutex);

        uint32_t uploaded = 0;
        size_t bytesUploaded = 0;

        while (!uploadQueue.empty() && uploaded < config.maxTilesPerFrame
               && bytesUploaded < config.maxBytesPerFrame)
        {
            auto& tileData = uploadQueue.back();

            if (!tileData.valid)
            {
                uploadQueue.pop_back();
                continue;
            }

            uint32_t physTile = allocateOrEvictTile();
            if (physTile == SVT_INVALID_TILE) break;

            uploadTileChannels(physTile, tileData);

            // Update tracking
            cache.setTileMapping(physTile, tileData.coord);
            cache.touchTile(physTile, currentFrame);
            residentTiles[tileData.coord] = physTile;

            // Update page table
            auto entry = SVTPageTableEntry::encode(physTile, 0, true, tileData.channelMask);
            pageTable.setEntry(tileData.coord, entry);

            bytesUploaded += tileData.albedoData.size() + tileData.normalData.size()
                           + tileData.ormData.size() + tileData.emissionData.size()
                           + tileData.heightData.size();
            ++uploaded;

            uploadQueue.pop_back();
        }

        if (uploaded > 0)
        {
            pageTable.flushToGPU();
        }

        stats.tilesUploadedThisFrame += uploaded;
        stats.bytesUploadedThisFrame += bytesUploaded;
    }

    void SVTStreamManager::touchVisibleTiles(const uint32_t* feedbackData,
                                              uint32_t entryCount, uint64_t frame)
    {
        // Mark all resident tiles that appear in feedback as recently used
        for (auto& [coord, physTile] : residentTiles)
        {
            uint32_t idx = pageTable.getFlatIndex(coord);
            if (idx < entryCount && (feedbackData[idx] & SVT_FEEDBACK_REQUEST_FLAG))
            {
                cache.touchTile(physTile, frame);
            }
        }
    }

    void SVTStreamManager::processEvictions()
    {
        // Evict tiles when cache usage exceeds threshold
        float usageRatio = 1.0f - static_cast<float>(cache.getFreeTileCount()) / cache.getTileCount();
        if (usageRatio < 0.9f) return;  // Below 90%, no eviction needed

        float targetRatio = 0.8f;
        uint32_t targetFree = static_cast<uint32_t>(cache.getTileCount() * (1.0f - targetRatio));

        while (cache.getFreeTileCount() < targetFree)
        {
            uint32_t evicted = cache.evictLRU(currentFrame);
            if (evicted == SVT_INVALID_TILE) break;

            auto evictedInfo = cache.getTileInfo(evicted);
            residentTiles.erase(evictedInfo.virtualCoord);
            pageTable.clearEntry(evictedInfo.virtualCoord);
            cache.freeTile(evicted);
            ++stats.tilesEvictedThisFrame;
        }
    }

    bool SVTStreamManager::isTileResident(const VirtualTileCoord& coord) const
    {
        return residentTiles.find(coord) != residentTiles.end();
    }

    void SVTStreamManager::clear()
    {
        // Clear request queue
        while (!requestQueue.empty()) requestQueue.pop();

        {
            std::lock_guard<std::mutex> lock(uploadQueueMutex);
            uploadQueue.clear();
        }

        // Free all physical tiles
        for (auto& [coord, physTile] : residentTiles)
        {
            cache.freeTile(physTile);
        }
        residentTiles.clear();

        pageTable.clearAll();
        pageTable.flushToGPU();
    }

    void SVTStreamManager::generateTileAsync(const VirtualTileCoord& coord)
    {
        if (!tileProvider) return;

        // For now, generate synchronously. Can be made async with std::async later.
        auto tileData = tileProvider->generateTile(coord);

        if (tileData.valid)
        {
            std::lock_guard<std::mutex> lock(uploadQueueMutex);
            uploadQueue.push_back(std::move(tileData));
        }
    }

    float SVTStreamManager::calculatePriority(const VirtualTileCoord& coord, uint32_t feedbackMip) const
    {
        // Higher priority for lower mip levels (finer detail, closer to camera)
        float mipPriority = static_cast<float>(SVT_MAX_MIP_LEVELS - coord.mipLevel);

        // Boost if the feedback requested this exact mip
        float exactBoost = (coord.mipLevel == feedbackMip) ? 2.0f : 1.0f;

        return mipPriority * exactBoost;
    }
}
