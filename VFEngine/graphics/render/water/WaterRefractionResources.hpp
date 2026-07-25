#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "WaterGPUTypes.hpp"
#include <vulkan/vulkan.hpp>

namespace core
{
    class Device;
}

namespace render::water
{
    class WaterRefractionResources
    {
    public:
        explicit WaterRefractionResources(core::Device& device);
        ~WaterRefractionResources();

        WaterRefractionResources(const WaterRefractionResources&) = delete;
        WaterRefractionResources& operator=(const WaterRefractionResources&) = delete;

        void init(vk::Format swapchainFormat, uint32_t width, uint32_t height, vk::ImageView sceneDepthView);
        void recreate(vk::Format swapchainFormat, uint32_t width, uint32_t height, vk::ImageView sceneDepthView);
        void cleanup();

        void copySceneColor(vk::CommandBuffer cmd, vk::Image srcColorImage, uint32_t width, uint32_t height);

        // VK-1604: memcpy into the persistently-mapped set 9 binding 2 UBO. Same single-buffered
        // per-frame update model the water tile SSBO and the caustics params already use.
        void updateParams(const WaterExtendedParams& params);

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Image refractionImage;
        core::VulkanAllocation refractionAllocation;
        vk::ImageView refractionView;

        vk::Sampler refractionSampler;
        // VK-1604: binding 1 samples the scene depth image. Nearest only - linear filtering of a
        // depth format is not a guaranteed format feature, and bilinear across a depth
        // discontinuity fabricates an in-between depth that the SSR thickness test accepts as a hit.
        vk::Sampler depthSampler;

        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsAllocation;
        void* paramsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;

        void createRefractionImage(vk::Format format, uint32_t width, uint32_t height);
        void createSampler();
        void createParamsBuffer();
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptorSet(vk::ImageView sceneDepthView);
        void transitionImageInitial();
    };
}
