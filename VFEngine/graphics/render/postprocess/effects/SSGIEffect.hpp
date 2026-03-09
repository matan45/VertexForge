#pragma once

#include "../PostProcessEffect.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../../gi/GITypes.hpp"
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

    struct SSGIParamsUBO
    {
        glm::mat4 projection;
        glm::mat4 inverseProjection;
        glm::mat4 viewMatrix;
        glm::vec4 params;        // x=intensity, y=radius, z=thickness, w=rayCount
        glm::vec4 screenParams;  // x=width, y=height, z=1/width, w=1/height
        float nearPlane;
        float farPlane;
        int stepCount;
        float frameRandom;
    };

    class SSGIEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        // Ray trace output
        vk::Image ssgiRawImage;
        vk::DeviceMemory ssgiRawMemory;
        vk::ImageView ssgiRawImageView;
        vk::Framebuffer ssgiRawFramebuffer;

        // Denoised output
        vk::Image ssgiDenoisedImage;
        vk::DeviceMemory ssgiDenoisedMemory;
        vk::ImageView ssgiDenoisedImageView;
        vk::Framebuffer ssgiDenoisedFramebuffer;

        vk::ImageView depthOnlyImageView;

        vk::RenderPass ssgiRenderPass;
        vk::RenderPass denoiseRenderPass;

        std::shared_ptr<core::Shader> traceShader;
        std::shared_ptr<core::Shader> denoiseShader;
        std::shared_ptr<core::Shader> compositeShader;

        vk::Pipeline tracePipeline;
        vk::PipelineLayout tracePipelineLayout;

        vk::Pipeline denoisePipeline;
        vk::PipelineLayout denoisePipelineLayout;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        vk::DescriptorSetLayout traceDescriptorSetLayout;
        vk::DescriptorSetLayout denoiseDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet traceDescriptorSet;
        vk::DescriptorSet denoiseDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        float currentIntensity = 1.0f;
        float currentRadius = 2.0f;
        int currentRayCount = 8;
        int currentStepCount = 16;
        float currentThickness = 0.5f;

        vk::Extent2D currentExtent{};
        uint32_t frameCounter = 0;

    public:
        SSGIEffect(core::Device& device, core::SwapChain& swapChain,
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

        // GI-specific settings update
        void updateGIParameters(const gi::GISettings& giSettings);

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::SSGI; }
        uint32_t getPriority() const override { return 45; } // Before SSAO (50)

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
        void createTracePipeline();
        void createDenoisePipeline();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupImages();
        void cleanupPipelines();
        void updateParamsBuffer();
    };
}
