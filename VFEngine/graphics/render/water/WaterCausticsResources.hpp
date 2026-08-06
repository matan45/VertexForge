#pragma once

#include "../../core/VulkanMemoryManager.hpp"
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
        float shoreWetRange = 5.0f;
        // VK-1614 retired shoreWetDarkening and shoreWetRoughness. The shoreline no longer applies its
        // own material model — it now contributes to the single terrain wetness signal, so a darkening
        // multiplier and a roughness target here would be a second, conflicting model again.
        // shoreWetRoughness was in any case dead in situ: it was applied as
        // max(roughness, shoreWetRoughness) with a 0.15 default against a 0.9 terrain layer roughness
        // default, so the slider was a no-op across its whole plausible range.
        // The slots are kept as padding rather than removed so the UBO stays a 16-byte multiple and the
        // buffer size, descriptor and both shader blocks are otherwise untouched.
        float pad0 = 0.0f;
        float pad1 = 0.0f;
        float pad2 = 0.0f;
    };
    static_assert(sizeof(CausticParams) == 32,
                  "CausticParamsUBO is mirrored in mesh_terrain.glsl and mesh_shader_gpudriven.glsl");

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
        core::VulkanAllocation paramsAllocation;
        void* paramsMapped = nullptr;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
    };
}
