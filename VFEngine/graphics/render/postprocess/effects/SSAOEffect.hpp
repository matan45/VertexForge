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

    struct SSAOParamsUBO
    {
        glm::mat4 projection;
        glm::mat4 inverseProjection;
        glm::vec4 params;         // radius, bias, intensity, power
        glm::vec2 noiseScale;
        int32_t kernelSize;
        float nearPlane;
        float farPlane;
        float padding[3];
    };

    class SSAOEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        // SSAO raw output image (R8Unorm)
        vk::Image ssaoRawImage;
        vk::DeviceMemory ssaoRawMemory;
        vk::ImageView ssaoRawImageView;
        vk::Framebuffer ssaoRawFramebuffer;

        // SSAO blurred output image (R8Unorm)
        vk::Image ssaoBlurredImage;
        vk::DeviceMemory ssaoBlurredMemory;
        vk::ImageView ssaoBlurredImageView;
        vk::Framebuffer ssaoBlurredFramebuffer;

        vk::ImageView depthOnlyImageView;

        vk::RenderPass ssaoRenderPass;
        vk::RenderPass blurRenderPass;

        std::shared_ptr<core::Shader> ssaoShader;
        std::shared_ptr<core::Shader> blurShader;
        std::shared_ptr<core::Shader> compositeShader;

        vk::Pipeline ssaoPipeline;
        vk::PipelineLayout ssaoPipelineLayout;

        vk::Pipeline blurPipeline;
        vk::PipelineLayout blurPipelineLayout;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout ssaoDescriptorSetLayout;
        vk::DescriptorSetLayout blurDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet ssaoDescriptorSet;
        vk::DescriptorSet blurDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        float currentRadius = 0.5f;
        float currentBias = 0.025f;
        float currentIntensity = 1.0f;
        int currentKernelSize = 32;
        float currentPower = 2.0f;

        vk::Extent2D currentExtent{};

    public:
        SSAOEffect(core::Device& device, core::SwapChain& swapChain,
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

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::SSAO; }
        uint32_t getPriority() const override { return 50; }

    private:
        void createSampler();
        void createRenderPasses();
        void createImages();
        void createDepthImageView();
        void createParamsBuffer();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createSSAOPipeline();
        void createBlurPipeline();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupImages();
        void cleanupPipelines();
        void updateParamsBuffer();
    };
}
