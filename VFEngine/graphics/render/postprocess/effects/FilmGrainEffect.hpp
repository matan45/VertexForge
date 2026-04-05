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
    class FilmGrainEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        float currentIntensity = 0.1f;
        float currentSize = 1.6f;

    public:
        explicit FilmGrainEffect(core::Device& device);

        void init(vk::Format colorFormat, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::Format colorFormat, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::FilmGrain; }
        uint32_t getPriority() const override { return 500; }
        bool isPreUpscale() const override { return false; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::Format colorFormat, vk::Extent2D extent);
    };
}
