#pragma once

#include "VegetationBufferManager.hpp"
#include <unordered_map>
#include <queue>
#include <cstdint>
#include <glm/glm.hpp>

namespace core { class Device; }

namespace render::vegetation
{
    struct GrassStreamConfig
    {
        uint64_t memoryBudgetBytes = 256 * 1024 * 1024; // 256MB default
        uint32_t maxUploadsPerFrame = 4;
        uint32_t maxBytesPerFrame = 2 * 1024 * 1024;    // 2MB per frame
        float evictionThreshold = 0.9f;
        float worldTileSize = 32.0f;
    };

    class GrassStreamManager
    {
    public:
        void init(core::Device& device, VegetationBufferManager& bufferManager);
        void cleanup();

        void update(const glm::vec3& cameraPos);

        void markTileDirty(int32_t coordX, int32_t coordZ);
        void addTile(int32_t coordX, int32_t coordZ);
        void removeTile(int32_t coordX, int32_t coordZ);

        void setConfig(const GrassStreamConfig& config) { streamConfig = config; }
        [[nodiscard]] const GrassStreamConfig& getConfig() const { return streamConfig; }

        [[nodiscard]] uint64_t getCurrentMemoryUsage() const { return currentMemoryUsage; }
        [[nodiscard]] size_t getPendingUploadCount() const { return pendingUploads.size(); }

    private:
        struct TileStreamState
        {
            VegetationTileKey key;
            bool isUploaded = false;
            bool isDirty = false;
            float priority = 0.0f;
            uint64_t memoryUsage = 0;
        };

        float calculatePriority(const VegetationTileKey& key, const glm::vec3& cameraPos) const;

        core::Device* devicePtr = nullptr;
        VegetationBufferManager* bufferManagerPtr = nullptr;
        GrassStreamConfig streamConfig;

        std::unordered_map<VegetationTileKey, TileStreamState, VegetationTileKeyHash> tileStates;
        std::vector<VegetationTileKey> pendingUploads;
        uint64_t currentMemoryUsage = 0;
    };
}
