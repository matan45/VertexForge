#pragma once

#include "VegetationBufferManager.hpp"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace core { class Device; }

namespace render::vegetation
{
    struct VegetationStreamConfig
    {
        uint64_t memoryBudgetBytes = 512 * 1024 * 1024; // 512MB default
        uint32_t maxUploadsPerFrame = 8;
        uint32_t maxBytesPerFrame = 4 * 1024 * 1024;
        float evictionThreshold = 0.9f;
    };

    class VegetationStreamManager
    {
    public:
        void init(core::Device& device, VegetationBufferManager& bufferManager);
        void cleanup();

        void update(const glm::vec3& cameraPos);

        void addTile(int32_t coordX, int32_t coordZ);
        void removeTile(int32_t coordX, int32_t coordZ);
        void markTileDirty(int32_t coordX, int32_t coordZ);

        void setConfig(const VegetationStreamConfig& config) { streamConfig = config; }
        [[nodiscard]] const VegetationStreamConfig& getConfig() const { return streamConfig; }

        [[nodiscard]] uint64_t getCurrentMemoryUsage() const { return currentMemoryUsage; }

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
        VegetationStreamConfig streamConfig;

        std::unordered_map<VegetationTileKey, TileStreamState, VegetationTileKeyHash> tileStates;
        uint64_t currentMemoryUsage = 0;
    };
}
