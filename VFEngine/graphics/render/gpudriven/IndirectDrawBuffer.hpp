#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>

namespace core {
    class Device;
}

namespace render::gpudriven {

    class IndirectDrawBuffer {
    public:
        explicit IndirectDrawBuffer(core::Device& device);
        ~IndirectDrawBuffer();

        // Non-copyable
        IndirectDrawBuffer(const IndirectDrawBuffer&) = delete;
        IndirectDrawBuffer& operator=(const IndirectDrawBuffer&) = delete;

        // Initialize with maximum draw command count
        void init(uint32_t maxCommands = MAX_DRAW_COMMANDS);

        // Cleanup GPU resources
        void cleanup();

        // Reset draw count to 0 (called at start of frame before culling)
        void resetDrawCount(vk::CommandBuffer cmd);

        // Insert barrier after compute shader writes, before indirect draw
        void insertBarrierAfterCompute(vk::CommandBuffer cmd);

        // Accessors
        vk::Buffer getDrawCommandBuffer() const { return drawCommandBuffer; }
        vk::Buffer getDrawCountBuffer() const { return drawCountBuffer; }
        vk::Buffer getPerDrawDataBuffer() const { return perDrawDataBuffer; }

        uint32_t getMaxDrawCommands() const { return maxDrawCommands; }

        // Read back draw count for debugging (expensive - causes sync)
        uint32_t readBackDrawCount();

        // Statistics
        size_t getDrawCommandBufferSize() const {
            return maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
        }
        size_t getPerDrawDataBufferSize() const {
            return maxDrawCommands * sizeof(PerDrawData);
        }

    private:
        core::Device& device;

        // Buffer for VkDrawIndexedIndirectCommand array (filled by compute shader)
        vk::Buffer drawCommandBuffer;
        vk::DeviceMemory drawCommandMemory;

        // Buffer for atomic draw count (single uint32_t, used with vkCmdDrawIndexedIndirectCount)
        vk::Buffer drawCountBuffer;
        vk::DeviceMemory drawCountMemory;

        // Buffer for per-draw data (consumed by vertex/fragment shaders)
        // Written alongside draw commands by compute shader
        vk::Buffer perDrawDataBuffer;
        vk::DeviceMemory perDrawDataMemory;

        // Staging buffer for resetting count and readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;

        uint32_t maxDrawCommands = 0;
        bool initialized = false;

        void createBuffers();
        void destroyBuffers();
    };

}
