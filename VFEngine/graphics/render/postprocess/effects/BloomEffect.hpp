#pragma once

#include "../PostProcessEffect.hpp"
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::postprocess
{
    struct BloomMipLevel
    {
        vk::ImageView imageView;
        vk::Framebuffer framebuffer;
        vk::DescriptorSet descriptorSet;
        uint32_t width;
        uint32_t height;
    };

    class BloomEffect : public PostProcessEffect
    {
    private:
        core::Device& device;

        // Mip chain
        vk::Image bloomImage;
        vk::DeviceMemory bloomMemory;
        std::vector<BloomMipLevel> mipLevels;
        uint32_t mipCount = 0;

        // Render passes
        vk::RenderPass downsampleRenderPass;
        vk::RenderPass upsampleRenderPass;

        // Shaders
        std::shared_ptr<core::Shader> downsampleShader;
        std::shared_ptr<core::Shader> upsampleShader;
        std::shared_ptr<core::Shader> compositeShader;

        // Pipelines (downsample/upsample use dynamic viewport/scissor)
        vk::Pipeline downsamplePipeline;
        vk::Pipeline upsamplePipeline;
        vk::PipelineLayout bloomPipelineLayout;

        // Composite pipeline (2 descriptor sets: scene + bloom)
        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        // Descriptors
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::Sampler bloomSampler;

        // Cached settings
        float currentThreshold = 1.0f;
        float currentIntensity = 0.5f;
        float currentRadius = 0.5f;
        uint32_t currentPasses = 5;

        // Stored extent for recreate
        vk::Extent2D currentExtent{};

    public:
        explicit BloomEffect(core::Device& device);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;

        void preRecord(const vk::CommandBuffer& commandBuffer,
                       vk::DescriptorSet inputDescriptorSet) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::Bloom; }
        uint32_t getPriority() const override { return 150; }

    private:
        void createSampler();
        void createRenderPasses();
        void createDescriptorSetLayout();
        void createMipChain();
        void createDescriptorPool();
        void createDescriptorSets();
        void loadShaders();
        void createBloomPipelines();
        void createCompositePipeline(vk::RenderPass externalRenderPass);

        void cleanupMipChain();
        void cleanupPipelines();

        void updateDescriptorSet(vk::DescriptorSet set, vk::ImageView imageView);
    };
}
