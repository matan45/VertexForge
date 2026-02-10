#pragma once

#include "../PostProcessEffect.hpp"
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

    struct GodRaysSunData
    {
        glm::vec2 sunScreenPos;
        float intensity;
        float decay;
        float density;
        float weight;
        int32_t sampleCount;
        float threshold;
    };

    class GodRaysEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        PostProcessPipeline& pipeline;

        // God rays result image
        vk::Image rayImage;
        vk::DeviceMemory rayMemory;
        vk::ImageView rayImageView;
        vk::Framebuffer rayFramebuffer;

        // Depth-only image view for sampling
        vk::ImageView depthOnlyImageView;

        // Internal render pass
        vk::RenderPass rayRenderPass;

        // Shaders
        std::shared_ptr<core::Shader> rayShader;
        std::shared_ptr<core::Shader> compositeShader;

        // Ray pass pipeline
        vk::Pipeline rayPipeline;
        vk::PipelineLayout rayPipelineLayout;

        // Composite pipeline
        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        // Descriptor layouts
        vk::DescriptorSetLayout rayDescriptorSetLayout;
        vk::DescriptorSetLayout compositeDescriptorSetLayout;

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet rayDescriptorSet;
        vk::DescriptorSet compositeDescriptorSet;

        // Sun UBO
        vk::Buffer sunBuffer;
        vk::DeviceMemory sunBufferMemory;
        void* sunBufferMapped = nullptr;

        // Sampler
        vk::Sampler sampler;

        // Depth aspect mask (depends on depth format)
        vk::ImageAspectFlags depthAspectMask;

        // Cached settings
        float currentIntensity = 0.8f;
        float currentDecay = 0.98f;
        float currentDensity = 1.0f;
        float currentWeight = 1.0f;
        int currentSampleCount = 64;
        float currentThreshold = 0.5f;

        vk::Extent2D currentExtent{};

    public:
        GodRaysEffect(core::Device& device, core::SwapChain& swapChain,
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

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::GodRays; }
        uint32_t getPriority() const override { return 40; }

    private:
        void createSampler();
        void createRayRenderPass();
        void createRayImage();
        void createDepthImageView();
        void createSunBuffer();
        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createRayPipeline();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupRayImage();
        void cleanupPipelines();
        void updateSunBuffer();
    };
}
