#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::volumetric
{
    class VolumetricPipeline;

    struct VolumetricCompositeParams
    {
        float nearPlane;
        float farPlane;
        float intensity;
        float padding0;
        uint32_t gridWidth;
        uint32_t gridHeight;
        uint32_t gridDepth;
        uint32_t padding1;
    };

    class VolumetricFogComposite
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        VolumetricPipeline* volumetricPipeline = nullptr;

        vk::RenderPass renderPass;
        std::vector<vk::Framebuffer> framebuffers;

        std::shared_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        vk::ImageView depthOnlyImageView;
        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        float currentIntensity = 1.0f;
        float cachedNear = 0.1f;
        float cachedFar = 1000.0f;
        vk::Extent2D currentExtent{};
        bool initialized = false;

    public:
        VolumetricFogComposite(core::Device& device, core::SwapChain& swapChain,
                               core::OffscreenResources& offscreenResources);
        ~VolumetricFogComposite();

        VolumetricFogComposite(const VolumetricFogComposite&) = delete;
        VolumetricFogComposite& operator=(const VolumetricFogComposite&) = delete;

        void init(VolumetricPipeline* volPipeline);
        void cleanup();
        void recreate();

        void execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        void setCameraData(float nearPlane, float farPlane) { cachedNear = nearPlane; cachedFar = farPlane; }
        void setIntensity(float intensity) { currentIntensity = intensity; }

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createRenderPass();
        void createFramebuffers();
        void createSampler();
        void createDepthImageView();
        void createParamsBuffer();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void loadShader();
        void createPipeline();

        void updateParamsBuffer();
        void cleanupFramebuffers();
        void cleanupPipeline();
    };
}
