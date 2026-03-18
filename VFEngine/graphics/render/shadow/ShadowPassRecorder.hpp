#pragma once

#include "ShadowTypes.hpp"
#include "VSMTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace core
{
    class Device;
    class ThreadCommandPoolManager;
}

namespace render::shadow
{
    class VSMPhysicalTilePool;
    class ShadowResourcePool;
    class ShadowPassPipeline;
    class TerrainShadowPipeline;

    struct ShadowPassParams
    {
        vk::DescriptorSet perDrawDataDescSet;
        vk::DescriptorSet meshletDataDescSet;
        vk::DescriptorSet vertexDataDescSet;
        vk::DescriptorSet boneMatrixDescSet;
        vk::DescriptorSet cameraDescSet;
        vk::Buffer drawCommandBuffer;
        vk::Buffer drawCountBuffer;
        uint32_t batchCount;
        uint32_t commandsPerSection;
        uint32_t shaderGroupCount;
        uint32_t transparentGroupIndex;
        uint32_t drawCountStructSize;
    };

    // Page render entry from ShadowSystem
    struct PageRenderEntry
    {
        uint32_t physicalTileIndex;
        glm::mat4 cropViewProjection;
        float depthBias;
        float slopeBias;
        float normalBias;
    };

    struct ShadowRecordingStats
    {
        float recordingUs = 0.0f;       // Total CPU time for shadow recording
        uint32_t tileCount = 0;         // Tiles recorded
        uint32_t threadsUsed = 0;       // Threads that participated (0 = inline)
        bool usedParallel = false;
    };

    class ShadowPassRecorder
    {
    private:
        core::Device& device;
        ShadowRecordingStats lastStats{};

    public:
        explicit ShadowPassRecorder(core::Device& device);
        ~ShadowPassRecorder() = default;

        ShadowPassRecorder(const ShadowPassRecorder&) = delete;
        ShadowPassRecorder& operator=(const ShadowPassRecorder&) = delete;

        void recordShadowPass(
            vk::CommandBuffer cmd,
            const ShadowPassParams& params,
            const TerrainShadowPassParams* terrainParams,
            VSMPhysicalTilePool* tilePool,
            ShadowResourcePool* resourcePool,
            ShadowPassPipeline* shadowPassPipeline,
            TerrainShadowPipeline* terrainShadowPipeline,
            const std::vector<PageRenderEntry>& pageRenderList,
            std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
            bool shadowsEnabled,
            bool poolFirstUse);

        // Parallel version: records VSM tiles across worker threads using secondary command buffers
        void recordShadowPassParallel(
            vk::CommandBuffer primaryCmd,
            const ShadowPassParams& params,
            const TerrainShadowPassParams* terrainParams,
            VSMPhysicalTilePool* tilePool,
            ShadowResourcePool* resourcePool,
            ShadowPassPipeline* shadowPassPipeline,
            TerrainShadowPipeline* terrainShadowPipeline,
            const std::vector<PageRenderEntry>& pageRenderList,
            std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
            bool shadowsEnabled,
            bool poolFirstUse,
            core::ThreadCommandPoolManager* threadPoolManager,
            uint32_t frameIndex);

        const ShadowRecordingStats& getLastStats() const { return lastStats; }

    private:
        void renderPointLightCubeShadows(
            vk::CommandBuffer cmd,
            const ShadowPassParams& params,
            const TerrainShadowPassParams* terrainParams,
            ShadowResourcePool* resourcePool,
            ShadowPassPipeline* shadowPassPipeline,
            TerrainShadowPipeline* terrainShadowPipeline,
            std::unordered_map<uint32_t, LightShadowData>& lightShadowData);
    };
}
