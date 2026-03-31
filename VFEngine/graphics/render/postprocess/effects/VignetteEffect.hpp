#pragma once

#include "../PostProcessEffect.hpp"
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::postprocess
{
    class VignetteEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        float currentIntensity = 0.3f;
        float currentRadius = 0.8f;
        float currentSoftness = 0.5f;

    public:
        explicit VignetteEffect(core::Device& device);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::Vignette; }
        uint32_t getPriority() const override { return 400; }
        bool isPreUpscale() const override { return false; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::RenderPass renderPass, vk::Extent2D extent);
    };
}
