#pragma once

#include "../PostProcessEffect.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include "postprocess/PostProcessTypes.hpp"
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

    struct EdgeDetectionPushConstants
    {
        float threshold;
        float edgeWidth;
        float edgeColorR;
        float edgeColorG;
        float edgeColorB;
        float opacity;
        float nearPlane;
        float farPlane;
    };

    class EdgeDetectionEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        vk::Image intermediateImage;
        core::VulkanAllocation intermediateAllocation;
        vk::ImageView intermediateImageView;
        vk::Framebuffer intermediateFramebuffer;

        vk::ImageView depthOnlyImageView;
        vk::RenderPass edgeRenderPass;

        std::shared_ptr<core::Shader> edgeShader;
        std::shared_ptr<core::Shader> compositeShader;

        vk::Pipeline edgePipeline;
        vk::PipelineLayout edgePipelineLayout;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout edgeDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet edgeDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        float currentThreshold = 0.1f;
        float currentEdgeWidth = 1.0f;
        float currentEdgeColor[3] = {0.0f, 0.0f, 0.0f};
        float currentOpacity = 1.0f;

        vk::Extent2D currentExtent{};

    public:
        EdgeDetectionEffect(core::Device& device, core::SwapChain& swapChain,
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

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::EdgeDetection; }
        uint32_t getPriority() const override { return 250; }
        bool isPreUpscale() const override { return false; }

    private:
        void createSampler();
        void createEdgeRenderPass();
        void createIntermediateImage();
        void createDepthImageView();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createEdgePipeline();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupIntermediateImage();
        void cleanupPipelines();
    };
}
