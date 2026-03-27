#pragma once

#include <vulkan/vulkan.hpp>

namespace core
{
    class Device;
}

namespace render::water
{
    struct CausticParams
    {
        float waterHeight = 0.0f;
        float causticStrength = 1.0f;
        float depthFalloff = 0.5f;
        float patchSize = 100.0f;
    };

    class WaterCausticsResources
    {
    public:
        explicit WaterCausticsResources(core::Device& device);
        ~WaterCausticsResources();

        WaterCausticsResources(const WaterCausticsResources&) = delete;
        WaterCausticsResources& operator=(const WaterCausticsResources&) = delete;

        void init(vk::ImageView causticView);
        void cleanup();

        void updateParams(const CausticParams& params);

        [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
        [[nodiscard]] vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Sampler causticSampler;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsMemory;
        void* paramsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
    };
}
