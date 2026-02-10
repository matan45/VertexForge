#pragma once

#include <vulkan/vulkan.hpp>
#include "postprocess/PostProcessTypes.hpp"

namespace core
{
    class Device;
}

namespace render::postprocess
{
    class PostProcessEffect
    {
    public:
        virtual ~PostProcessEffect() = default;

        // Lifecycle - receives shared render pass + extent from pipeline
        virtual void init(vk::RenderPass renderPass, vk::Extent2D extent) = 0;
        virtual void cleanup() = 0;
        virtual void recreate(vk::RenderPass renderPass, vk::Extent2D extent) = 0;

        // Called before beginRenderPass — multi-pass effects do internal work here
        virtual void preRecord(const vk::CommandBuffer& commandBuffer,
                               vk::DescriptorSet inputDescriptorSet) {}

        // Record fullscreen draw (called between beginRenderPass/endRenderPass by pipeline)
        virtual void record(const vk::CommandBuffer& commandBuffer,
                            vk::DescriptorSet inputDescriptorSet) = 0;

        // Update effect parameters from settings
        virtual void updateParameters(const ::postprocess::PostProcessSettings& settings) = 0;

        // Properties
        virtual ::postprocess::EffectType getType() const = 0;
        virtual uint32_t getPriority() const = 0;

        bool isEnabled() const { return enabled; }
        void setEnabled(bool value) { enabled = value; }
        bool isInitialized() const { return initialized; }

    protected:
        bool enabled = false;
        bool initialized = false;
    };
}
