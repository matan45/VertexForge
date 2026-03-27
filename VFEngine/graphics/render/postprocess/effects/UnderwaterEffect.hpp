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
    class PostProcessPipeline;

    class UnderwaterEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        PostProcessPipeline& pipeline;

        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        ::postprocess::UnderwaterSettings currentSettings;

    public:
        UnderwaterEffect(core::Device& device, PostProcessPipeline& pipeline);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;
        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::Underwater; }
        uint32_t getPriority() const override { return 55; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::RenderPass renderPass, vk::Extent2D extent);
    };
}
