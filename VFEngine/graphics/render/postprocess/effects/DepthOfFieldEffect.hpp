#pragma once

#include "../PostProcessEffect.hpp"
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

    struct DoFParams
    {
        float focalDistance;
        float focalRange;
        float maxBlurRadius;
        float nearPlane;
        float farPlane;
        int32_t sampleCount;
        float padding[2];
    };

    class DepthOfFieldEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        // Blur result image (RGBA - rgb = blurred color, a = CoC)
        vk::Image blurImage;
        vk::DeviceMemory blurMemory;
        vk::ImageView blurImageView;
        vk::Framebuffer blurFramebuffer;

        // Depth-only image view for sampling
        vk::ImageView depthOnlyImageView;

        // Internal render pass
        vk::RenderPass blurRenderPass;

        // Shaders
        std::shared_ptr<core::Shader> blurShader;
        std::shared_ptr<core::Shader> compositeShader;

        // Blur pass pipeline
        vk::Pipeline blurPipeline;
        vk::PipelineLayout blurPipelineLayout;

        // Composite pipeline
        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        // Descriptor layouts
        vk::DescriptorSetLayout blurDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet blurDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        // DoF UBO
        vk::Buffer dofBuffer;
        vk::DeviceMemory dofBufferMemory;
        void* dofBufferMapped = nullptr;

        // Sampler
        vk::Sampler sampler;

        // Depth aspect mask (depends on depth format)
        vk::ImageAspectFlags depthAspectMask;

        // Cached settings
        ::postprocess::DoFFocusMode currentFocusMode = ::postprocess::DoFFocusMode::Manual;
        float currentFocalDistance = 10.0f;
        glm::vec3 currentFocusTarget{0.0f};
        float currentFocusSmoothing = 5.0f;
        float currentFocalRange = 5.0f;
        float currentMaxBlurRadius = 5.0f;
        int currentSampleCount = 32;

        // Smoothing state
        float smoothedFocalDistance = 10.0f;
        float lastTime = 0.0f;

        vk::Extent2D currentExtent{};

    public:
        DepthOfFieldEffect(core::Device& device, core::SwapChain& swapChain,
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

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::DepthOfField; }
        uint32_t getPriority() const override { return 60; }

    private:
        void createSampler();
        void createBlurRenderPass();
        void createBlurImage();
        void createDepthImageView();
        void createDoFBuffer();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createBlurPipeline();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupBlurImage();
        void cleanupPipelines();
        void updateDoFBuffer();
    };
}
