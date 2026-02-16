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
                  vk::DescriptorSetLayout lightCullingDescLayout);
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet volumetricGridDescSet,
                      vk::DescriptorSet clusterGridDescSet,
                      vk::DescriptorSet lightBufferDescSet,
                      vk::DescriptorSet lightCullingDescSet,
                      uint32_t frameIndex);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createPipelineLayout();
        void createComputePipeline();
    };
}
