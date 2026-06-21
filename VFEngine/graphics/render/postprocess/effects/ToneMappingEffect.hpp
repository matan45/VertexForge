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
    class ToneMappingEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        float currentExposure = 1.0f;
        float currentGamma = 2.2f;
        float currentContrast = 1.0f;
        float currentToe = 0.0f;
        float currentShoulder = 0.0f;
        ::postprocess::ToneMappingMode currentMode = ::postprocess::ToneMappingMode::ACES;

    public:
        explicit ToneMappingEffect(core::Device& device);

        void init(vk::Format colorFormat, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::Format colorFormat, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::ToneMapping; }
        uint32_t getPriority() const override { return 100; }
        bool isPreUpscale() const override { return false; }

        void setExposureOverride(float value) { currentExposure = value; }

        // VK-1419: layout of the set-0/binding-0 combined-image-sampler input. Render textures own
        // their own ToneMappingEffect and allocate input descriptor sets against this layout.
        vk::DescriptorSetLayout getInputDescriptorSetLayout() const { return inputDescriptorSetLayout; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::Format colorFormat, vk::Extent2D extent);
    };
}
