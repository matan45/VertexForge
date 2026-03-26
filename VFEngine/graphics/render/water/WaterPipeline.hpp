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
    class OceanFFT;

    struct WaterPipelineLayoutConfig
    {
        vk::DescriptorSetLayout iblLayout;
        vk::DescriptorSetLayout lightDataLayout;
        vk::DescriptorSetLayout clusterGridLayout;
        vk::DescriptorSetLayout cullingOutputLayout;
        vk::DescriptorSetLayout shadowDataLayout;
        vk::DescriptorSetLayout shadowTextureLayout;
        vk::DescriptorSetLayout oceanTextureLayout; // Optional: from OceanFFT
        vk::DescriptorSetLayout refractionLayout;  // Optional: from WaterRefractionResources
        vk::RenderPass renderPass;
    };

    struct WaterRenderDescriptors
    {
        vk::DescriptorSet iblDescSet;
        vk::DescriptorSet lightDataDescSet;
        vk::DescriptorSet clusterGridDescSet;
        vk::DescriptorSet cullingOutputDescSet;
        vk::DescriptorSet shadowDataDescSet;
        vk::DescriptorSet shadowTextureDescSet;
        vk::DescriptorSet oceanTextureDescSet; // Optional: from OceanFFT
        vk::DescriptorSet refractionDescSet;  // Optional: from WaterRefractionResources
    };

    class WaterPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> waterShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout waterTileLayout;
        vk::DescriptorPool waterTilePool;
        vk::DescriptorSet waterTileDescriptorSet;

        vk::DescriptorSetLayout dudvTextureLayout;
        vk::DescriptorPool dudvTexturePool;
        vk::DescriptorSet dudvTextureDescriptorSet;

        vk::Image dudvImage;
        vk::DeviceMemory dudvImageMemory;
        vk::ImageView dudvImageView;
        vk::Sampler dudvSampler;

        vk::DescriptorSetLayout cachedIBLLayout;
        vk::DescriptorSetLayout cachedLightDataLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedCullingOutputLayout;
        vk::DescriptorSetLayout cachedShadowDataLayout;
        vk::DescriptorSetLayout cachedShadowTextureLayout;

        // Refraction support
        vk::DescriptorSetLayout refractionLayout;         // Currently active layout (dummy or external)
        vk::DescriptorSetLayout refractionDummyLayout;
        vk::DescriptorPool refractionDummyPool;
        vk::DescriptorSet refractionDummyDescSet;

        // Ocean FFT texture support
        vk::DescriptorSetLayout oceanTextureLayout;       // Currently active layout (dummy or external)
        vk::DescriptorSetLayout oceanDummyLayout;         // Our owned dummy layout
        vk::DescriptorPool oceanDummyPool;
        vk::DescriptorSet oceanDummyDescSet;
        vk::Image oceanDummyImage;
        vk::DeviceMemory oceanDummyMemory;
        vk::ImageView oceanDummyView;
        vk::Sampler oceanDummySampler;
        bool initialized = false;
        uint32_t lastDescriptorTileCount = 0;

    public:
        explicit WaterPipeline(core::Device& device, core::SwapChain& swapChain);
        ~WaterPipeline();

        void init(const WaterPipelineLayoutConfig& config);
        void recreate(const WaterPipelineLayoutConfig& config);
        void cleanup();

        void updateDescriptors(vk::Buffer tileSSBO, uint32_t tileCount);

        void render(vk::CommandBuffer cmd, const WaterRenderDescriptors& descriptors,
                    WaterMeshBuffer& meshBuffer, const WaterPushConstants& pushConstants);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createWaterTileDescriptor();
        void createDuDvTexture();
        void createDuDvDescriptor();
        void createOceanDummyTexture();
        void createRefractionDummy();
        void createGraphicsPipeline(const WaterPipelineLayoutConfig& config);
    };
}
