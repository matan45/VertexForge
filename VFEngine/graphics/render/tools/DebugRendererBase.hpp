#pragma once

#include <vulkan/vulkan.hpp>
#include "../../core/VulkanMemoryManager.hpp"

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::mesh
{
    class DebugRendererBase
    {
    protected:
        core::Device& device;
        core::SwapChain& swapChain;
        bool initialized = false;

        explicit DebugRendererBase(core::Device& device, core::SwapChain& swapChain);
        ~DebugRendererBase() = default;

        void destroyPipelineAndLayout(vk::Pipeline& pipeline, vk::PipelineLayout& layout);
        void destroyBufferPair(vk::Buffer& buffer, core::VulkanAllocation& alloc);

    public:
        DebugRendererBase(const DebugRendererBase&) = delete;
        DebugRendererBase& operator=(const DebugRendererBase&) = delete;

        [[nodiscard]] bool isInitialized() const { return initialized; }
    };
}
