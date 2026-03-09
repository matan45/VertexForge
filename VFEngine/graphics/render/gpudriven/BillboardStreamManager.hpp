#pragma once

#include "BillboardGPUTypes.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>

namespace core
{
    class Device;
}

namespace render::gpudriven
{
    class BindlessTextureManager;

    struct BillboardStreamConfig
    {
        uint64_t memoryBudgetBytes = 256 * 1024 * 1024; // 256MB default
        uint32_t maxUploadsPerFrame = 16;
        float loadDistance = 500.0f;      // Start loading at this distance
        float unloadDistance = 600.0f;    // Unload beyond this distance
        float evictionThreshold = 0.9f;
    };

    class BillboardStreamManager
    {
    public:
        void init(core::Device& device);
        void cleanup();

        void update(const glm::vec3& cameraPos,
                    const std::vector<BillboardInstanceGPU>& allInstances,
                    std::vector<BillboardInstanceGPU>& visibleInstances);

        void setConfig(const BillboardStreamConfig& config) { streamConfig = config; }
        [[nodiscard]] const BillboardStreamConfig& getConfig() const { return streamConfig; }
        [[nodiscard]] uint64_t getCurrentMemoryUsage() const { return currentMemoryUsage; }
        [[nodiscard]] uint32_t getStreamedInstanceCount() const { return streamedCount; }

    private:
        core::Device* devicePtr = nullptr;
        BillboardStreamConfig streamConfig;
        uint64_t currentMemoryUsage = 0;
        uint32_t streamedCount = 0;
    };
}
