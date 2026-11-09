#pragma once

#include "../PostProcessEffect.hpp"
#include "../../../core/RenderManager.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

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

    // auto_exposure_reduce.glsl's `Exposure` storage buffer and push_constant block, field for field (std430; pinned
    // by test_auto_exposure).
    struct AutoExposureState
    {
        float currentExposure;
        float targetExposure;
        float avgLuminance;
        float padding;
    };

    // VK-1664: ONE adaptation chain (`state`, read and written by the reduce every frame) plus a host copy per frame in
    // flight that the reduce writes too. preRecord reads the copy written MAX_FRAMES_IN_FLIGHT frames earlier, whose
    // submission has completed. Per-frame buffers that the reduce both read and wrote ran two interleaved chains, each
    // adapting at half the configured speed, and the tonemapper alternated between them.
    struct AutoExposureBuffer
    {
        AutoExposureState state;
        std::array<AutoExposureState, core::MAX_FRAMES_IN_FLIGHT> readback;
    };

    struct AutoExposureReducePushConstants
    {
        float minLogLuminance;
        float logLuminanceRange;
        float lowPercentile;
        float highPercentile;
        float adaptSpeedUp;
        float adaptSpeedDown;
        float deltaTime;
        float exposureCompensation;
        float minExposure;
        float maxExposure;
        uint32_t pixelCount;
        uint32_t readbackSlot; // the AutoExposureBuffer::readback entry this frame writes
    };

    static_assert(core::MAX_FRAMES_IN_FLIGHT == 2, "auto_exposure_reduce.glsl declares readback[2]");
    static_assert(sizeof(AutoExposureBuffer) == 48, "keep in step with auto_exposure_reduce.glsl's Exposure buffer");
    static_assert(sizeof(AutoExposureReducePushConstants) == 48,
                  "keep in step with auto_exposure_reduce.glsl's push_constant block");

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
        // VK-1655: one histogram set per offscreen colour image (scene view + histogram buffer), written once
        // in createDescriptorSets; preRecord binds the one for PostProcessPipeline::getCurrentImageIndex().
        std::vector<vk::DescriptorSet> histogramDescriptorSets;
        // VK-1664: histogram + exposureBuffer, written once in createDescriptorSets (init / recreate), never while a
        // command buffer that uses it may be pending.
        vk::DescriptorSet reduceDescriptorSet;

        vk::Buffer histogramBuffer;
        core::VulkanAllocation histogramBufferAllocation;

        // VK-1664: the AutoExposureBuffer (adaptation state + per-frame readback), host visible and coherent, persistently
        // mapped. currentExposureFrame is the readback slot this frame reads on the CPU and then writes on the GPU.
        vk::Buffer exposureBuffer;
        core::VulkanAllocation exposureAllocation;
        AutoExposureBuffer* exposureMapped = nullptr;
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
        // VK-1664: the adaptation clock, the steady_clock time of the previous AE step (FilmGrainEffect keeps its own
        // clock too). firstFrame: no step since init.
        std::chrono::steady_clock::time_point previousStepTime{};
        bool firstFrame = true;

        vk::Extent2D currentExtent{};

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
        void updateHistogramDescriptorSet(vk::DescriptorSet set, vk::ImageView sceneImageView);
        void writeReduceDescriptorSet();
        void cleanupBuffers();
        void cleanupPipelines();
    };
}
