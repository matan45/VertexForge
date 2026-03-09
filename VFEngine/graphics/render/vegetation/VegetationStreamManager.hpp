#pragma once

#include "VegetationBufferManager.hpp"
#include "../gpudriven/BillboardGPUTypes.hpp"
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
        float worldTileSize = 32.0f;
    };

    // Tree instance that should transition to billboard impostor
    struct VegetationBillboardRequest
    {
        glm::vec3 position{0.0f};
        float scale = 1.0f;
        uint32_t bindlessTextureIndex = 0;
        glm::vec4 atlasUVRect{0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec2 size{2.0f, 4.0f};
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

        // Billboard impostor integration
        void addBillboardRequest(const VegetationBillboardRequest& request);
        void clearBillboardRequests();
        [[nodiscard]] const std::vector<gpudriven::BillboardInstanceGPU>& getBillboardInstances() const { return billboardInstances; }
        void buildBillboardInstances();

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

        // Billboard impostor data
        std::vector<VegetationBillboardRequest> billboardRequests;
        std::vector<gpudriven::BillboardInstanceGPU> billboardInstances;
    };
}
