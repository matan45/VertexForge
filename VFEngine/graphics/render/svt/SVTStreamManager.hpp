#pragma once

#include "SVTTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <cstdint>

namespace render::svt
{
    class PhysicalTileCache;
    class SVTPageTable;

    // Tile data produced by a tile provider (e.g., terrain compositor)
    struct SVTTileData
    {
        VirtualTileCoord coord;
        std::vector<uint8_t> albedoData;   // BC7 compressed
        std::vector<uint8_t> normalData;   // BC7 compressed
        std::vector<uint8_t> ormData;      // BC7 compressed
        bool valid = false;
    };

    // Interface for producing tile data on demand
    class SVTTileProvider
    {
    public:
        virtual ~SVTTileProvider() = default;

        // Generate tile data for a given virtual tile coordinate.
        // Called from a worker thread — must be thread-safe.
        virtual SVTTileData generateTile(const VirtualTileCoord& coord) = 0;
    };

    struct SVTStreamStats
    {
        uint32_t totalPhysicalTiles = 0;
        uint32_t usedPhysicalTiles = 0;
        uint32_t pendingRequests = 0;
        uint32_t tilesUploadedThisFrame = 0;
        uint32_t tilesEvictedThisFrame = 0;
        size_t bytesUploadedThisFrame = 0;
    };

    struct TileRequest
    {
        VirtualTileCoord coord;
        float priority = 0.0f;  // Higher = more important

        bool operator<(const TileRequest& other) const
        {
            return priority < other.priority;  // Max-heap
        }
    };

    // Manages the SVT streaming pipeline:
    //   1. Reads GPU feedback to find requested tiles
    //   2. Prioritizes and queues tile generation
    //   3. Uploads tiles to PhysicalTileCache
    //   4. Updates SVTPageTable
    //   5. Handles LRU eviction
    class SVTStreamManager
    {
    private:
        PhysicalTileCache& cache_;
        SVTPageTable& pageTable_;
        SVTConfig config_;

        SVTTileProvider* tileProvider_ = nullptr;

        // Request tracking
        std::priority_queue<TileRequest> requestQueue_;
        std::unordered_map<VirtualTileCoord, uint32_t, VirtualTileCoordHash> residentTiles_;
        // Maps virtual tile coord -> physical tile index

        // Completed tiles waiting for GPU upload
        std::vector<SVTTileData> uploadQueue_;
        std::mutex uploadQueueMutex_;

        SVTStreamStats stats_;
        uint64_t currentFrame_ = 0;

        bool initialized_ = false;

    public:
        SVTStreamManager(PhysicalTileCache& cache, SVTPageTable& pageTable);
        ~SVTStreamManager() = default;

        void init(const SVTConfig& config);

        void setTileProvider(SVTTileProvider* provider) { tileProvider_ = provider; }

        // Process feedback data and update streaming state.
        // Call once per frame after reading back feedback.
        void processFeedback(const uint32_t* feedbackData, uint32_t entryCount,
                             uint64_t frameIndex, const glm::vec3& cameraPos);

        // Upload ready tiles to GPU. Call before rendering.
        void processUploads();

        // Touch all visible resident tiles (update LRU)
        void touchVisibleTiles(const uint32_t* feedbackData, uint32_t entryCount, uint64_t frame);

        // Evict tiles when cache is near full
        void processEvictions();

        // Full per-frame update (calls processFeedback, processUploads, processEvictions)
        void update(const uint32_t* feedbackData, uint32_t entryCount,
                    uint64_t frameIndex, const glm::vec3& cameraPos);

        // Check if a virtual tile is currently resident
        bool isTileResident(const VirtualTileCoord& coord) const;

        const SVTStreamStats& getStats() const { return stats_; }

        void clear();

    private:
        void generateTileAsync(const VirtualTileCoord& coord);
        float calculatePriority(const VirtualTileCoord& coord, uint32_t feedbackMip) const;
    };
}
