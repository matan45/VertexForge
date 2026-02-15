#include "DebugRendererBase.hpp"
#include "../../core/Device.hpp"

namespace render::mesh
{
    DebugRendererBase::DebugRendererBase(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    void DebugRendererBase::destroyPipelineAndLayout(vk::Pipeline& pipeline, vk::PipelineLayout& layout)
    {
        auto& dev = device.getLogicalDevice();

        if (pipeline)
        {
            dev.destroyPipeline(pipeline);
            pipeline = nullptr;
        }
        if (layout)
        {
            dev.destroyPipelineLayout(layout);
            layout = nullptr;
        }
    }

    void DebugRendererBase::destroyBufferPair(vk::Buffer& buffer, vk::DeviceMemory& memory)
    {
        auto& dev = device.getLogicalDevice();

        if (buffer)
        {
            dev.destroyBuffer(buffer);
            buffer = nullptr;
        }
        if (memory)
        {
            dev.freeMemory(memory);
            memory = nullptr;
        }
    }
}
