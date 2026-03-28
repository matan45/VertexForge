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

        vk::Image blurImage;
        core::VulkanAllocation blurAllocation;
        vk::ImageView blurImageView;
        vk::Framebuffer blurFramebuffer;

        vk::ImageView depthOnlyImageView;
        vk::RenderPass blurRenderPass;

        std::shared_ptr<core::Shader> blurShader;
        std::shared_ptr<core::Shader> compositeShader;

        vk::Pipeline blurPipeline;
        vk::PipelineLayout blurPipelineLayout;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout blurDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet blurDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Buffer dofBuffer;
        core::VulkanAllocation dofBufferAllocation;
        void* dofBufferMapped = nullptr;

        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        ::postprocess::DoFFocusMode currentFocusMode = ::postprocess::DoFFocusMode::Manual;
        float currentFocalDistance = 10.0f;
        glm::vec3 currentFocusTarget{0.0f};
        float currentFocusSmoothing = 5.0f;
        float currentFocalRange = 5.0f;
        float currentMaxBlurRadius = 5.0f;
        int currentSampleCount = 32;

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
