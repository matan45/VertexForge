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
    class VolumetricRayMarch
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout volumetricGridLayout; // Set 0

        VolumetricGridDimensions dims{};
        bool initialized = false;

    public:
        explicit VolumetricRayMarch(core::Device& device);
        ~VolumetricRayMarch();

        VolumetricRayMarch(const VolumetricRayMarch&) = delete;
        VolumetricRayMarch& operator=(const VolumetricRayMarch&) = delete;

        void init(const VolumetricGridDimensions& dimensions,
                  vk::DescriptorSetLayout volumetricGridDescLayout);
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet volumetricGridDescSet);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createPipelineLayout();
        void createComputePipeline();
    };
}
