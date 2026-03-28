#pragma once

#include "../../core/VulkanMemoryManager.hpp"
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

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Image refractionImage;
        core::VulkanAllocation refractionAllocation;
        vk::ImageView refractionView;

        vk::Sampler refractionSampler;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;

        void createRefractionImage(vk::Format format, uint32_t width, uint32_t height);
        void createSampler();
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptorSet(vk::ImageView sceneDepthView);
        void transitionImageInitial();
    };
}
