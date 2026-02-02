#pragma once

#include "ShadowTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class ShadowAtlasManager;
    class ShadowResourcePool;
    class ShadowPassPipeline;
    class TerrainShadowPipeline;

    struct ShadowPassParams
    {
        vk::DescriptorSet perDrawDataDescSet;
        vk::DescriptorSet meshletDataDescSet;
        vk::DescriptorSet vertexDataDescSet;
        vk::DescriptorSet boneMatrixDescSet;
        vk::Buffer drawCommandBuffer;
        vk::Buffer drawCountBuffer;
        uint32_t batchCount;
        uint32_t commandsPerSection;
        uint32_t shaderGroupCount;
        uint32_t drawCountStructSize;
    };

    class ShadowPassRecorder
    {
    private:
        core::Device& device;
        bool atlasFirstUse = true;

    public:
        explicit ShadowPassRecorder(core::Device& device);
        ~ShadowPassRecorder() = default;

        ShadowPassRecorder(const ShadowPassRecorder&) = delete;
        ShadowPassRecorder& operator=(const ShadowPassRecorder&) = delete;

        void recordShadowPass(
            vk::CommandBuffer cmd,
            const ShadowPassParams& params,
            const TerrainShadowPassParams* terrainParams,
            ShadowAtlasManager* atlasManager,
            ShadowResourcePool* resourcePool,
            ShadowPassPipeline* shadowPassPipeline,
            TerrainShadowPipeline* terrainShadowPipeline,
            const std::vector<ShadowView>& directionalShadowViews,
            const std::vector<ShadowView>& spotShadowViews,
            std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
            bool shadowsEnabled);

        void resetAtlasFirstUse() { atlasFirstUse = true; }
        [[nodiscard]] bool isAtlasFirstUse() const { return atlasFirstUse; }

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
