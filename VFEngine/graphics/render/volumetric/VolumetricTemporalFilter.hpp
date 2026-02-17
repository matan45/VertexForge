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
    class VolumetricTemporalFilter
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
        explicit VolumetricTemporalFilter(core::Device& device);
        ~VolumetricTemporalFilter();

        VolumetricTemporalFilter(const VolumetricTemporalFilter&) = delete;
        VolumetricTemporalFilter& operator=(const VolumetricTemporalFilter&) = delete;

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
