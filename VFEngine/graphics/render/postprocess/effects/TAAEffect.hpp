#pragma once

#include "../PostProcessEffect.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include <glm/glm.hpp>
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

    struct TAAParamsUBO
    {
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec2 jitterOffset;
        glm::vec2 texelSize;
        float blendFactor;
        uint32_t frameIndex;
        uint32_t useVarianceClipping;
        uint32_t historyValid;
    };

    struct TAASharpenPushConstants
    {
        float texelSizeX;
        float texelSizeY;
        float sharpenStrength;
    };

    class TAAEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        // History buffers (double-buffered)
        struct HistoryBuffer
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView imageView;
            vk::Framebuffer framebuffer;
        };
        HistoryBuffer historyBuffers[2]{};
        uint32_t currentHistoryIndex = 0;
        bool historyValid = false;

        vk::ImageView depthOnlyImageView;
        vk::ImageAspectFlags depthAspectMask;

        vk::RenderPass taaRenderPass;

        std::shared_ptr<core::Shader> resolveShader;
        std::shared_ptr<core::Shader> sharpenShader;

        vk::Pipeline resolvePipeline;
        vk::PipelineLayout resolvePipelineLayout;

        vk::Pipeline sharpenPipeline;
        vk::PipelineLayout sharpenPipelineLayout;

        vk::DescriptorSetLayout resolveDescriptorSetLayout;
        vk::DescriptorSetLayout sharpenDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet resolveDescriptorSets[2]; // one per history read target
        vk::DescriptorSet sharpenDescriptorSets[2]; // one per history write target

        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsBufferAllocation;
        void* paramsBufferMapped = nullptr;

        vk::Sampler sampler;

        float currentBlendFactor = 0.1f;
        float currentSharpenStrength = 0.5f;
        bool currentUseVarianceClipping = true;

        vk::Extent2D currentExtent{};

    public:
        TAAEffect(core::Device& device, core::SwapChain& swapChain,
                  core::OffscreenResources& offscreenResources,
                  PostProcessPipeline& pipeline);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;

        void preRecord(const vk::CommandBuffer& commandBuffer,
                       vk::DescriptorSet inputDescriptorSet) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::TAA; }
        uint32_t getPriority() const override { return 5; }

    private:
        void createSampler();
        void createRenderPass();
        void createHistoryBuffers();
        void createDepthImageView();
        void createParamsBuffer();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createResolvePipeline();
        void createSharpenPipeline(vk::RenderPass externalRenderPass);

        void cleanupHistoryBuffers();
        void cleanupPipelines();
        void updateParamsBuffer(vk::DescriptorSet inputDescriptorSet);
    };
}
