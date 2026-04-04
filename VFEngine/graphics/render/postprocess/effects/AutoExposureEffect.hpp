#pragma once

#include "../PostProcessEffect.hpp"
#include "../../../core/RenderManager.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <array>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::postprocess
{
    class PostProcessPipeline;

    class AutoExposureEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        std::shared_ptr<core::Shader> histogramShader;
        std::shared_ptr<core::Shader> reduceShader;
        std::shared_ptr<core::Shader> passthroughShader;

        vk::Pipeline histogramPipeline;
        vk::PipelineLayout histogramPipelineLayout;

        vk::Pipeline reducePipeline;
        vk::PipelineLayout reducePipelineLayout;

        vk::Pipeline passthroughPipeline;
        vk::PipelineLayout passthroughPipelineLayout;

        vk::DescriptorSetLayout histogramDescriptorSetLayout;
        vk::DescriptorSetLayout reduceDescriptorSetLayout;
        vk::DescriptorSetLayout passthroughDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet histogramDescriptorSet;
        vk::DescriptorSet reduceDescriptorSet;

        vk::Buffer histogramBuffer;
        core::VulkanAllocation histogramBufferAllocation;

        struct ExposureFrame
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            void* mapped = nullptr;
        };

        std::array<ExposureFrame, core::MAX_FRAMES_IN_FLIGHT> exposureFrames{};
        uint32_t currentExposureFrame = 0;

        vk::Sampler sceneSampler;

        float minExposure = 0.1f;
        float maxExposure = 10.0f;
        float adaptSpeedUp = 3.0f;
        float adaptSpeedDown = 1.0f;
        float exposureCompensation = 0.0f;
        float lowPercentile = 0.1f;
        float highPercentile = 0.9f;

        float computedExposure = 1.0f;
        float previousTime = 0.0f;
        bool firstFrame = true;

        vk::Extent2D currentExtent{};
        vk::ImageView lastSceneImageView{};

    public:
        AutoExposureEffect(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources,
                           PostProcessPipeline& pipeline);

        void init(vk::Format colorFormat, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::Format colorFormat, vk::Extent2D extent) override;

        void preRecord(const vk::CommandBuffer& commandBuffer,
                       vk::DescriptorSet inputDescriptorSet) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::AutoExposure; }
        uint32_t getPriority() const override { return 90; }

        float getComputedExposure() const { return computedExposure; }

    private:
        void createSampler();
        void createBuffers();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createHistogramPipeline();
        void createReducePipeline();
        void createPassthroughPipeline(vk::Format colorFormat, vk::Extent2D extent);
        void updateHistogramDescriptorSet(vk::ImageView sceneImageView);
        void updateReduceDescriptorSet();
        void cleanupBuffers();
        void cleanupPipelines();
    };
}
