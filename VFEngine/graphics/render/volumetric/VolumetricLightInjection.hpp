#pragma once

#include "VolumetricTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::volumetric
{
    class VolumetricLightInjection
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;

        // External descriptor set layouts (not owned)
        vk::DescriptorSetLayout volumetricGridLayout;   // Set 0
        vk::DescriptorSetLayout clusterGridLayout;       // Set 1
        vk::DescriptorSetLayout lightBufferLayout;       // Set 2
        vk::DescriptorSetLayout lightCullingLayout;      // Set 3
        vk::DescriptorSetLayout shadowDataLayout;        // Set 4
        vk::DescriptorSetLayout shadowTextureLayout;     // Set 5
        vk::DescriptorSetLayout fogVolumeLayout;          // Set 6
        vk::DescriptorSetLayout giSamplingLayout;           // Set 7

        VolumetricGridDimensions dims{};
        bool initialized = false;

    public:
        explicit VolumetricLightInjection(core::Device& device);
        ~VolumetricLightInjection();

        VolumetricLightInjection(const VolumetricLightInjection&) = delete;
        VolumetricLightInjection& operator=(const VolumetricLightInjection&) = delete;

        void init(const VolumetricGridDimensions& dimensions,
                  vk::DescriptorSetLayout volumetricGridDescLayout,
                  vk::DescriptorSetLayout clusterGridDescLayout,
                  vk::DescriptorSetLayout lightBufferDescLayout,
                  vk::DescriptorSetLayout lightCullingDescLayout,
                  vk::DescriptorSetLayout shadowDataDescLayout,
                  vk::DescriptorSetLayout shadowTextureDescLayout,
                  vk::DescriptorSetLayout fogVolumeDescLayout,
                  vk::DescriptorSetLayout giSamplingDescLayout);
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet volumetricGridDescSet,
                      vk::DescriptorSet clusterGridDescSet,
                      vk::DescriptorSet lightBufferDescSet,
                      vk::DescriptorSet lightCullingDescSet,
                      vk::DescriptorSet shadowDataDescSet,
                      vk::DescriptorSet shadowTextureDescSet,
                      vk::DescriptorSet fogVolumeDescSet,
                      vk::DescriptorSet giSamplingDescSet,
                      uint32_t frameIndex);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createPipelineLayout();
        void createComputePipeline();
    };
}
