#pragma once

#include "TerrainGPUAdapter.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <queue>

namespace terrain
{
    class TerrainTile;
}

namespace render::gpudriven
{
    class TerrainMeshBuffer;

    enum class TerrainTileStreamState : uint8_t
    {
        NotLoaded,
        FallbackOnly,
        Streaming,
        FullyLoaded
    };

    struct TerrainTileStreamInfo
    {
        TerrainTileKey key;
        TerrainTileStreamState state = TerrainTileStreamState::NotLoaded;
        uint8_t currentLoadedLOD = 255;
        uint8_t targetLOD = 3;
        float distanceToCamera = 0.0f;
        float priority = 0.0f;
        uint64_t lastAccessFrame = 0;
        size_t gpuMemoryUsage = 0;

        uint8_t loadedLODMask = 0;

        bool hasLODLoaded(uint8_t lod) const { return (loadedLODMask & (1 << lod)) != 0; }
        void setLODLoaded(uint8_t lod) { loadedLODMask |= (1 << lod); }
        void clearLODLoaded(uint8_t lod) { loadedLODMask &= ~(1 << lod); }
    };

    struct TerrainStreamConfig
    {
        size_t memoryBudgetBytes = 512 * 1024 * 1024;
        size_t maxBytesPerFrame = 4 * 1024 * 1024;
        uint32_t maxUploadsPerFrame = 8;
        uint32_t maxFallbackUploadsPerFrame = 16;
        size_t maxFallbackBytesPerFrame = 2 * 1024 * 1024;
        bool keepFallbackLoaded = true;
        float evictionThreshold = 0.9f;
    };

    struct TerrainStreamingStats
    {
        size_t memoryUsedBytes = 0;
        size_t memoryBudgetBytes = 0;
        uint32_t tilesLoaded = 0;
        uint32_t tilesStreaming = 0;
        uint32_t uploadsThisFrame = 0;
        size_t bytesUploadedThisFrame = 0;
        uint32_t fallbackTiles = 0;
        uint32_t fullDetailTiles = 0;
    };

    struct StreamPriorityEntry
    {
        TerrainTileKey key;
        uint8_t targetLOD;
        float priority;

        bool operator<(const StreamPriorityEntry& other) const
        {
            return priority < other.priority;
        }
    };

    class TerrainStreamManager
    {
    private:
        TerrainMeshBuffer& terrainBuffer;
        TerrainGPUAdapter& adapter;

        TerrainStreamConfig config;
        TerrainStreamingStats stats;

        std::unordered_map<TerrainTileKey, TerrainTileStreamInfo, TerrainTileKeyHash> tileInfos;
        std::priority_queue<StreamPriorityEntry> uploadQueue;

        uint64_t currentFrame = 0;
        size_t currentMemoryUsage = 0;

        static constexpr size_t LOD_MEMORY_ESTIMATE[4] = {
            6 * 1024 * 1024,    // LOD0: ~6MB
            1536 * 1024,        // LOD1: ~1.5MB
            400 * 1024,         // LOD2: ~400KB
            100 * 1024          // LOD3: ~100KB
        };

    public:
        TerrainStreamManager(TerrainMeshBuffer& terrainBuf,
                             TerrainGPUAdapter& gpuAdapter);
        ~TerrainStreamManager();

        TerrainStreamManager(const TerrainStreamManager&) = delete;
        TerrainStreamManager& operator=(const TerrainStreamManager&) = delete;

        void update(const std::vector<terrain::TerrainTile*>& visibleTiles,
                    const glm::vec3& cameraPosition);

        void setMemoryBudget(size_t bytes) { config.memoryBudgetBytes = bytes; }
        size_t getMemoryBudget() const { return config.memoryBudgetBytes; }

        const TerrainStreamingStats& getStats() const { return stats; }

        void clear();

    private:
        float calculatePriority(float distance,
                                uint8_t targetLOD,
                                uint8_t currentLOD) const;

        uint8_t selectTargetLOD(float distance) const;

        void processEvictions(const glm::vec3& cameraPosition);

        void evictTileLOD(const TerrainTileKey& key, uint8_t lodLevel);

        size_t estimateLODMemory(const terrain::TerrainTile& tile, uint8_t lodLevel) const;
    };
}
