#pragma once

#include "GITypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gi
{
    class ProbeUpdatePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> updateShader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;

        bool initialized = false;

    public:
        explicit ProbeUpdatePipeline(core::Device& device);
        ~ProbeUpdatePipeline();

        ProbeUpdatePipeline(const ProbeUpdatePipeline&) = delete;
        ProbeUpdatePipeline& operator=(const ProbeUpdatePipeline&) = delete;

        void init(vk::DescriptorSetLayout probeDataLayout,
                  vk::DescriptorSetLayout cascadeInfoLayout);
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet probeWriteDescSet,
                      vk::DescriptorSet cascadeInfoDescSet,
                      const GIComputePushConstants& pushConstants);

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                  vk::DescriptorSetLayout cascadeInfoLayout);
        void createComputePipeline();
    };
}
