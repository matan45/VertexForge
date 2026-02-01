#pragma once

#include "GPUDrivenTypes.hpp"
#include "TerrainGPUAdapter.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <queue>
#include <cstdint>

namespace terrain
{
    class TerrainTile;
}

namespace render::gpudriven
{
    class TerrainMeshBuffer;

    // Stream state for a terrain tile
    enum class TerrainTileStreamState : uint8_t
    {
        NotLoaded,       // No GPU data uploaded
        FallbackOnly,    // Only LOD3 loaded (lowest detail)
        Streaming,       // Higher LODs being uploaded
        FullyLoaded      // Target LOD loaded
    };

    // Per-tile streaming information
    struct TerrainTileStreamInfo
    {
        TerrainTileKey key;
        TerrainTileStreamState state = TerrainTileStreamState::NotLoaded;
        uint8_t currentLoadedLOD = 255;   // Which LOD is in GPU memory (255 = none)
        uint8_t targetLOD = 3;            // Desired LOD based on camera distance
        float distanceToCamera = 0.0f;
        float priority = 0.0f;
        uint64_t lastAccessFrame = 0;     // For LRU eviction
        size_t gpuMemoryUsage = 0;

        // Bitmask of which LODs are currently loaded in GPU memory
        uint8_t loadedLODMask = 0;

        bool hasLODLoaded(uint8_t lod) const { return (loadedLODMask & (1 << lod)) != 0; }
        void setLODLoaded(uint8_t lod) { loadedLODMask |= (1 << lod); }
        void clearLODLoaded(uint8_t lod) { loadedLODMask &= ~(1 << lod); }
    };

    // Configuration for terrain streaming
    struct TerrainStreamConfig
    {
        size_t memoryBudgetBytes = 512 * 1024 * 1024;  // 512MB default
        size_t maxBytesPerFrame = 4 * 1024 * 1024;     // 4MB per frame max upload
        uint32_t maxUploadsPerFrame = 8;               // Max tile LOD uploads per frame
        bool keepFallbackLoaded = true;                // Always keep LOD3 for visible tiles
        float evictionThreshold = 0.9f;                // Start evicting at 90% budget
    };

    // Streaming statistics
    struct TerrainStreamingStats
    {
        size_t memoryUsedBytes = 0;
        size_t memoryBudgetBytes = 0;
        uint32_t tilesLoaded = 0;
        uint32_t tilesStreaming = 0;
        uint32_t tilesEvicted = 0;
        uint32_t uploadsThisFrame = 0;
        size_t bytesUploadedThisFrame = 0;
        uint32_t fallbackTiles = 0;        // Tiles rendering with LOD3 fallback
        uint32_t fullDetailTiles = 0;      // Tiles at target LOD
    };

    // Priority entry for streaming queue
    struct StreamPriorityEntry
    {
        TerrainTileKey key;
        uint8_t targetLOD;
        float priority;

        bool operator<(const StreamPriorityEntry& other) const
        {
            return priority < other.priority; // Lower priority = later in queue
        }
    };

    // Manages streaming of terrain tiles to GPU memory
    class TerrainStreamManager
    {
    private:
        TerrainMeshBuffer& terrainBuffer_;
        TerrainGPUAdapter& adapter_;

        TerrainStreamConfig config_;
        TerrainStreamingStats stats_;

        std::unordered_map<TerrainTileKey, TerrainTileStreamInfo, TerrainTileKeyHash> tileInfos_;
        std::priority_queue<StreamPriorityEntry> uploadQueue_;

        uint64_t currentFrame_ = 0;
        size_t currentMemoryUsage_ = 0;

        // Estimated memory per LOD level (bytes)
        // These are approximations based on vertex/index/meshlet counts
        static constexpr size_t LOD_MEMORY_ESTIMATE[4] = {
            6 * 1024 * 1024,    // LOD0: ~6MB (Ultra: 66K vertices)
            1536 * 1024,        // LOD1: ~1.5MB (16K vertices)
            400 * 1024,         // LOD2: ~400KB (4K vertices)
            100 * 1024          // LOD3: ~100KB (1K vertices)
        };

    public:
        TerrainStreamManager(TerrainMeshBuffer& terrainBuffer,
                             TerrainGPUAdapter& adapter);
        ~TerrainStreamManager();

        TerrainStreamManager(const TerrainStreamManager&) = delete;
        TerrainStreamManager& operator=(const TerrainStreamManager&) = delete;

        // Main update - call each frame with visible tiles and camera position
        // Decides what to load, stream, and evict
        void update(const std::vector<terrain::TerrainTile*>& visibleTiles,
                    const glm::vec3& cameraPosition);

        // Configuration
        void setConfig(const TerrainStreamConfig& config) { config_ = config; }
        const TerrainStreamConfig& getConfig() const { return config_; }
        void setMemoryBudget(size_t bytes) { config_.memoryBudgetBytes = bytes; }
        size_t getMemoryBudget() const { return config_.memoryBudgetBytes; }

        // Query tile state
        const TerrainTileStreamInfo* getTileInfo(const TerrainTileKey& key) const;
        uint8_t getBestAvailableLOD(const TerrainTileKey& key) const;
        bool isTileLoaded(const TerrainTileKey& key) const;

        // Statistics
        const TerrainStreamingStats& getStats() const { return stats_; }
        size_t getCurrentMemoryUsage() const { return currentMemoryUsage_; }

        // Clear all streaming data (call on scene change)
        void clear();

        // Force immediate load of a tile at specific LOD (bypasses queue)
        bool forceLoadTile(const terrain::TerrainTile& tile, uint8_t lodLevel);

    private:
        // Priority calculation
        float calculatePriority(const terrain::TerrainTile& tile,
                                const glm::vec3& cameraPosition,
                                uint8_t targetLOD,
                                uint8_t currentLOD) const;

        // LOD selection based on distance
        uint8_t selectTargetLOD(float distance) const;

        // Process upload queue - uploads tiles up to bandwidth limit
        void processUploadQueue(const std::vector<terrain::TerrainTile*>& visibleTiles);

        // Eviction when approaching memory budget
        void processEvictions(const glm::vec3& cameraPosition);

        // Upload a single LOD for a tile
        bool uploadTileLOD(const terrain::TerrainTile& tile, uint8_t lodLevel);

        // Evict a single LOD from a tile
        void evictTileLOD(const TerrainTileKey& key, uint8_t lodLevel);

        // Get memory estimate for a tile at given LOD
        size_t estimateLODMemory(const terrain::TerrainTile& tile, uint8_t lodLevel) const;

        // Update tile info from visible tiles
        void updateTileInfos(const std::vector<terrain::TerrainTile*>& visibleTiles,
                             const glm::vec3& cameraPosition);

        // Build upload queue based on priorities
        void buildUploadQueue(const std::vector<terrain::TerrainTile*>& visibleTiles,
                              const glm::vec3& cameraPosition);
    };
}
