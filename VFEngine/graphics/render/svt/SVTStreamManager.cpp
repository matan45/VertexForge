#include "SVTStreamManager.hpp"
#include "PhysicalTileCache.hpp"
#include "SVTPageTable.hpp"
#include "print/Log.hpp"
#include <future>
#include <cstring>

namespace render::svt
{
    SVTStreamManager::SVTStreamManager(PhysicalTileCache& cache, SVTPageTable& pageTable)
        : cache_(cache), pageTable_(pageTable)
    {
    }

    void SVTStreamManager::init(const SVTConfig& config)
    {
        config_ = config;
        initialized_ = true;
    }

    void SVTStreamManager::update(const uint32_t* feedbackData, uint32_t entryCount,
                                   uint64_t frameIndex, const glm::vec3& cameraPos)
    {
        if (!initialized_ || !feedbackData) return;
        currentFrame_ = frameIndex;

        stats_.tilesUploadedThisFrame = 0;
        stats_.tilesEvictedThisFrame = 0;
        stats_.bytesUploadedThisFrame = 0;

        processFeedback(feedbackData, entryCount, frameIndex, cameraPos);
        touchVisibleTiles(feedbackData, entryCount, frameIndex);
        processEvictions();
        processUploads();

        stats_.totalPhysicalTiles = cache_.getTileCount();
        stats_.usedPhysicalTiles = cache_.getTileCount() - cache_.getFreeTileCount();
        stats_.pendingRequests = static_cast<uint32_t>(requestQueue_.size());
    }

    void SVTStreamManager::processFeedback(const uint32_t* feedbackData, uint32_t entryCount,
                                            uint64_t frameIndex, const glm::vec3& cameraPos)
    {
        // Scan feedback buffer for requested tiles
        uint32_t mipLevels = computeMipLevelCount(config_.virtualTextureSizeLog2, config_.tileSizeLog2);

        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            uint32_t mipOffset = computePageTableMipOffset(mip, config_.virtualTextureSizeLog2, config_.tileSizeLog2);
            uint32_t tilesPerSide = computeTilesPerMipSide(mip, config_.virtualTextureSizeLog2, config_.tileSizeLog2);
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

                    requestQueue_.push({coord, priority});
                }
            }
        }

        // Process top-priority requests up to budget
        uint32_t tilesGenerated = 0;
        while (!requestQueue_.empty() && tilesGenerated < config_.maxTilesPerFrame)
        {
            auto req = requestQueue_.top();
            requestQueue_.pop();

            // Double-check not already resident (may have been queued multiple times)
            if (isTileResident(req.coord)) continue;

            generateTileAsync(req.coord);
            ++tilesGenerated;
        }
    }

    void SVTStreamManager::processUploads()
    {
        std::lock_guard<std::mutex> lock(uploadQueueMutex_);

        uint32_t uploaded = 0;
        size_t bytesUploaded = 0;

        while (!uploadQueue_.empty() && uploaded < config_.maxTilesPerFrame
               && bytesUploaded < config_.maxBytesPerFrame)
        {
            auto& tileData = uploadQueue_.back();

            if (!tileData.valid)
            {
                uploadQueue_.pop_back();
                continue;
            }

            // Allocate physical tile slot
            uint32_t physTile = cache_.allocateTile();
            if (physTile == SVT_INVALID_TILE)
            {
                // Try eviction
                physTile = cache_.evictLRU(currentFrame_);
                if (physTile == SVT_INVALID_TILE)
                {
                    vfLogWarning("SVT: Cache full, cannot upload tile");
                    break;
                }

                // Remove evicted tile from resident map
                auto evictedInfo = cache_.getTileInfo(physTile);
                if (evictedInfo.occupied)
                {
                    residentTiles_.erase(evictedInfo.virtualCoord);
                    pageTable_.clearEntry(evictedInfo.virtualCoord);
                    ++stats_.tilesEvictedThisFrame;
                }
            }

            // Upload tile data to all three channels
            cache_.uploadTileData(physTile, 0, tileData.albedoData.data(),
                                  static_cast<uint32_t>(tileData.albedoData.size()));
            cache_.uploadTileData(physTile, 1, tileData.normalData.data(),
                                  static_cast<uint32_t>(tileData.normalData.size()));
            cache_.uploadTileData(physTile, 2, tileData.ormData.data(),
                                  static_cast<uint32_t>(tileData.ormData.size()));

            // Update tracking
            cache_.setTileMapping(physTile, tileData.coord);
            cache_.touchTile(physTile, currentFrame_);
            residentTiles_[tileData.coord] = physTile;

            // Update page table
            auto entry = SVTPageTableEntry::encode(physTile, physTile, physTile, 0, true);
            pageTable_.setEntry(tileData.coord, entry);

            bytesUploaded += tileData.albedoData.size() + tileData.normalData.size() + tileData.ormData.size();
            ++uploaded;

            uploadQueue_.pop_back();
        }

        if (uploaded > 0)
        {
            pageTable_.flushToGPU();
        }

        stats_.tilesUploadedThisFrame += uploaded;
        stats_.bytesUploadedThisFrame += bytesUploaded;
    }

    void SVTStreamManager::touchVisibleTiles(const uint32_t* feedbackData,
                                              uint32_t entryCount, uint64_t frame)
    {
        // Mark all resident tiles that appear in feedback as recently used
        for (auto& [coord, physTile] : residentTiles_)
        {
            uint32_t idx = pageTable_.getFlatIndex(coord);
            if (idx < entryCount && (feedbackData[idx] & SVT_FEEDBACK_REQUEST_FLAG))
            {
                cache_.touchTile(physTile, frame);
            }
        }
    }

    void SVTStreamManager::processEvictions()
    {
        // Evict tiles when cache usage exceeds threshold
        float usageRatio = 1.0f - static_cast<float>(cache_.getFreeTileCount()) / cache_.getTileCount();
        if (usageRatio < 0.9f) return;  // Below 90%, no eviction needed

        float targetRatio = 0.8f;
        uint32_t targetFree = static_cast<uint32_t>(cache_.getTileCount() * (1.0f - targetRatio));

        while (cache_.getFreeTileCount() < targetFree)
        {
            uint32_t evicted = cache_.evictLRU(currentFrame_);
            if (evicted == SVT_INVALID_TILE) break;

            auto evictedInfo = cache_.getTileInfo(evicted);
            residentTiles_.erase(evictedInfo.virtualCoord);
            pageTable_.clearEntry(evictedInfo.virtualCoord);
            cache_.freeTile(evicted);
            ++stats_.tilesEvictedThisFrame;
        }
    }

    bool SVTStreamManager::isTileResident(const VirtualTileCoord& coord) const
    {
        return residentTiles_.find(coord) != residentTiles_.end();
    }

    void SVTStreamManager::clear()
    {
        // Clear request queue
        while (!requestQueue_.empty()) requestQueue_.pop();

        {
            std::lock_guard<std::mutex> lock(uploadQueueMutex_);
            uploadQueue_.clear();
        }

        // Free all physical tiles
        for (auto& [coord, physTile] : residentTiles_)
        {
            cache_.freeTile(physTile);
        }
        residentTiles_.clear();

        pageTable_.clearAll();
        pageTable_.flushToGPU();
    }

    void SVTStreamManager::generateTileAsync(const VirtualTileCoord& coord)
    {
        if (!tileProvider_) return;

        // For now, generate synchronously. Can be made async with std::async later.
        auto tileData = tileProvider_->generateTile(coord);

        if (tileData.valid)
        {
            std::lock_guard<std::mutex> lock(uploadQueueMutex_);
            uploadQueue_.push_back(std::move(tileData));
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
