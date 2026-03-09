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
    class ProbeStorageBuffer;

    class ProbeTracePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> traceShader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;

        vk::DescriptorSetLayout traceDescriptorLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet traceDescriptorSet;

        bool initialized = false;

    public:
        explicit ProbeTracePipeline(core::Device& device);
        ~ProbeTracePipeline();

        ProbeTracePipeline(const ProbeTracePipeline&) = delete;
        ProbeTracePipeline& operator=(const ProbeTracePipeline&) = delete;

        void init(vk::DescriptorSetLayout probeDataLayout,
                  vk::DescriptorSetLayout cascadeInfoLayout,
                  vk::DescriptorSetLayout tlasLayout = nullptr,
                  vk::DescriptorSetLayout lightDataLayout = nullptr);
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::DescriptorSet probeWriteDescSet,
                      vk::DescriptorSet cascadeInfoDescSet,
                      vk::DescriptorSet probeReadDescSet,
                      const GIComputePushConstants& pushConstants,
                      vk::DescriptorSet tlasDescSet = nullptr,
                      vk::DescriptorSet lightDataDescSet = nullptr);

        bool isInitialized() const { return initialized; }
        bool hasRayQuery() const { return rayQueryEnabled; }

    private:
        bool rayQueryEnabled = false;

        bool hasLightData = false;

        void loadShader();
        void createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                  vk::DescriptorSetLayout cascadeInfoLayout,
                                  vk::DescriptorSetLayout tlasLayout,
                                  vk::DescriptorSetLayout lightDataLayout);
        void createComputePipeline();
    };
}
