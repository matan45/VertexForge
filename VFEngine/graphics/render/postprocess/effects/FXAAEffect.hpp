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
    class FXAAEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;
        vk::Extent2D currentExtent{};

        // Cached settings
        float currentEdgeThresholdMin = 0.0312f;
        float currentEdgeThreshold = 0.125f;
        ::postprocess::FXAAQuality currentQuality = ::postprocess::FXAAQuality::Medium;

    public:
        explicit FXAAEffect(core::Device& device);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::FXAA; }
        uint32_t getPriority() const override { return 200; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::RenderPass renderPass, vk::Extent2D extent);
    };
}
