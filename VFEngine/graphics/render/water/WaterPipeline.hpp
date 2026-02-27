#pragma once

#include "WaterGPUTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::water
{
    class WaterMeshBuffer;

    class WaterPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> waterShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        // Set 1: water tile SSBO descriptor
        vk::DescriptorSetLayout waterTileLayout;
        vk::DescriptorPool waterTilePool;
        vk::DescriptorSet waterTileDescriptorSet;

        // Set 2: dudv texture descriptor
        vk::DescriptorSetLayout dudvTextureLayout;
        vk::DescriptorPool dudvTexturePool;
        vk::DescriptorSet dudvTextureDescriptorSet;

        // Procedural dudv texture resources
        vk::Image dudvImage;
        vk::DeviceMemory dudvImageMemory;
        vk::ImageView dudvImageView;
        vk::Sampler dudvSampler;

        // Cached shared layouts
        vk::DescriptorSetLayout cachedIBLLayout;
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;
        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

        bool initialized = false;
        uint32_t lastDescriptorTileCount = 0;

    public:
        WaterPipeline(core::Device& device, core::SwapChain& swapChain);
        ~WaterPipeline();

        void init(vk::DescriptorSetLayout iblDescriptorSetLayout,
                  vk::DescriptorSetLayout lightDataLayout,
                  vk::DescriptorSetLayout clusterGridLayout,
                  vk::DescriptorSetLayout cullingOutputLayout,
                  vk::DescriptorSetLayout shadowDataLayout,
                  vk::DescriptorSetLayout shadowTextureLayout,
                  vk::RenderPass renderPass);
        void recreate(vk::DescriptorSetLayout iblDescriptorSetLayout,
                      vk::DescriptorSetLayout lightDataLayout,
                      vk::DescriptorSetLayout clusterGridLayout,
                      vk::DescriptorSetLayout cullingOutputLayout,
                      vk::DescriptorSetLayout shadowDataLayout,
                      vk::DescriptorSetLayout shadowTextureLayout,
                      vk::RenderPass renderPass);
        void cleanup();

        void updateDescriptors(vk::Buffer tileSSBO, uint32_t tileCount);

        void render(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                    vk::DescriptorSet lightDataDescSet,
                    vk::DescriptorSet clusterGridDescSet,
                    vk::DescriptorSet cullingOutputDescSet,
                    vk::DescriptorSet shadowDataDescSet,
                    vk::DescriptorSet shadowTextureDescSet,
                    WaterMeshBuffer& meshBuffer,
                    const WaterPushConstants& pushConstants);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createWaterTileDescriptor();
        void createDuDvTexture();
        void createDuDvDescriptor();
        void createGraphicsPipeline(vk::DescriptorSetLayout iblLayout,
                                    vk::DescriptorSetLayout lightDataLayout,
                                    vk::DescriptorSetLayout clusterGridLayout,
                                    vk::DescriptorSetLayout cullingOutputLayout,
                                    vk::DescriptorSetLayout shadowDataLayout,
                                    vk::DescriptorSetLayout shadowTextureLayout,
                                    vk::RenderPass renderPass);
    };
}
