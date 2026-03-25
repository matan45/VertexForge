#pragma once
#include "CloudTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core { class Device; class SwapChain; class Shader; struct OffscreenResources; }

namespace render::cloud
{
    class CloudRayMarch
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;

        // Half-res cloud result image (RGBA16F)
        vk::Image resultImage;
        vk::DeviceMemory resultMemory;
        vk::ImageView resultView;

        // UBO
        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        // Compute pipeline
        vk::DescriptorSetLayout dsLayout;
        vk::DescriptorPool dsPool;
        vk::DescriptorSet descriptorSet;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        std::shared_ptr<core::Shader> shader;

        vk::Extent2D halfExtent{};
        bool needsInitialTransition = true;

    public:
        CloudRayMarch(core::Device& device, core::SwapChain& swapChain);
        ~CloudRayMarch();

        CloudRayMarch(const CloudRayMarch&) = delete;
        CloudRayMarch& operator=(const CloudRayMarch&) = delete;

        void init(vk::ImageView shapeNoiseView, vk::ImageView detailNoiseView,
                  vk::ImageView weatherMapView, vk::Sampler noiseSampler,
                  vk::ImageView transmittanceView, vk::Sampler lutSampler,
                  vk::ImageView blueNoiseView);
        void cleanup();
        void recreate(vk::ImageView shapeNoiseView, vk::ImageView detailNoiseView,
                      vk::ImageView weatherMapView, vk::Sampler noiseSampler,
                      vk::ImageView transmittanceView, vk::Sampler lutSampler,
                      vk::ImageView blueNoiseView);

        void updateParams(const GPUCloudParams& params);
        void dispatch(const vk::CommandBuffer& cmd);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::ImageView getResultView() const { return resultView; }
        [[nodiscard]] vk::Image getResultImage() const { return resultImage; }
        [[nodiscard]] vk::Extent2D getHalfExtent() const { return halfExtent; }
    };
}
