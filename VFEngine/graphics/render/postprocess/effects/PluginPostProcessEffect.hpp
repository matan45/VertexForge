#pragma once

#include "../PostProcessEffect.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::postprocess
{
    // A plugin-registered full-screen post-process effect (VK-1409). The plugin
    // supplies only a fragment shader (sampling the scene HDR color at
    // set 0 / binding 0) plus an analytic params block (<= 64 bytes) and a chain
    // priority. The engine prepends its own fullscreen-triangle vertex stage,
    // owns the scene-color sampler / ping-pong / layout transitions (via
    // PostProcessPipeline), and runs this exactly like a built-in effect.
    //
    // Self-contained like ToneMappingEffect: it creates its own (identical)
    // input descriptor-set layout, so the combined-image-sampler descriptor sets
    // the pipeline binds at record() are layout-compatible.
    //
    // All state mutation (params, enabled) is driven on the render thread through
    // PostProcessPipeline's pending-ops queue, so no internal locking is needed.
    class PluginPostProcessEffect : public PostProcessEffect
    {
    public:
        PluginPostProcessEffect(core::Device& device,
                                std::string fragmentGlsl,
                                uint32_t priority,
                                uint32_t paramsSize,
                                bool startEnabled,
                                std::string debugName);

        void init(vk::Format colorFormat, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::Format colorFormat, vk::Extent2D extent) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        // Plugin effects are not driven by PostProcessSettings.
        void updateParameters(const ::postprocess::PostProcessSettings& settings) override {}

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::PluginCustom; }
        uint32_t getPriority() const override { return priority; }
        // Post-upscale: when upscaling is active only executePostUpscale runs
        // (executePreUpscale is currently never invoked), so a pre-upscale effect
        // would be dropped. As a post-upscale effect we run in both paths — at
        // render resolution in the non-upscale execute() (which ignores this flag
        // and runs every enabled effect by priority) and at display resolution in
        // executePostUpscale — always ordered by priority relative to the tonemap.
        bool isPreUpscale() const override { return false; }

        // Render-thread-only mutators (called from PostProcessPipeline drain).
        void setParams(const std::byte* data, size_t size);
        void setEnabledExternal(bool value) { enabled = value; }

    private:
        bool buildPipeline(vk::Format colorFormat, vk::Extent2D extent);
        std::string synthesizeSource() const;

        core::Device& device;
        std::string fragmentGlsl;
        uint32_t priority;
        uint32_t paramsSize;
        std::string debugName;

        std::shared_ptr<core::Shader> shader;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;

        std::vector<std::byte> paramBytes; // sized to paramsSize, pushed each record()
    };
}
