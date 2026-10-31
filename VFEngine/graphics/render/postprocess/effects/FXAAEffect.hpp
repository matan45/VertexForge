#pragma once

#include "../PostProcessEffect.hpp"
#include <cstdint>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::postprocess
{
    // fxaa.glsl's push_constant block, field for field (pinned by test_postprocess_fxaa).
    struct FXAAPushConstants
    {
        float subpixel = 0.75f;
        float edgeThreshold = 0.125f;
        float edgeThresholdMin = 0.0312f;
        uint32_t quality = static_cast<uint32_t>(::postprocess::FXAAQuality::Medium);
    };
    static_assert(sizeof(FXAAPushConstants) == 16, "keep in step with fxaa.glsl's push_constant block");

    // VK-1661: FXAA 3.11 Quality over the tone-mapped image. Post-upscale, so it runs after DLSS / DLAA at
    // display resolution, and at render resolution in the non-upscaled chain.
    class FXAAEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        FXAAPushConstants params;

    public:
        explicit FXAAEffect(core::Device& device);

        void init(vk::Format colorFormat, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::Format colorFormat, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::FXAA; }
        // After EdgeDetection (250), so its outlines are smoothed too; before ChromaticAberration (300),
        // Vignette (400) and FilmGrain (500).
        uint32_t getPriority() const override { return 275; }
        bool isPreUpscale() const override { return false; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createPipeline(vk::Format colorFormat, vk::Extent2D extent);
    };
}
