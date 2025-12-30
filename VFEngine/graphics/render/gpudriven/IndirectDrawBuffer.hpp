#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>

namespace core {
    class Device;
}

namespace render::gpudriven {

    // Stats buffer layout - must match shader's DrawCountBuffer
    struct GPUCullStats {
        uint32_t drawCount;
        uint32_t lodCount0;
        uint32_t lodCount1;
        uint32_t lodCount2;
        uint32_t lodCount3;
        uint32_t culledByFrustum;
        uint32_t culledByOcclusion;
    };

    class IndirectDrawBuffer {
    private:
        core::Device& device;
        
        vk::Buffer drawCommandBuffer;
        vk::DeviceMemory drawCommandMemory;
        
        vk::Buffer drawCountBuffer;
        vk::DeviceMemory drawCountMemory;
        
        vk::Buffer perDrawDataBuffer;
        vk::DeviceMemory perDrawDataMemory;
        
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;

        uint32_t maxDrawCommands = 0;
        bool initialized = false;
        
    public:
        explicit IndirectDrawBuffer(core::Device& device);
        ~IndirectDrawBuffer();
        
        IndirectDrawBuffer(const IndirectDrawBuffer&) = delete;
        IndirectDrawBuffer& operator=(const IndirectDrawBuffer&) = delete;
        
        void init(uint32_t maxCommands = MAX_DRAW_COMMANDS);
        
        void cleanup();
        
        void resetDrawCount(vk::CommandBuffer cmd);
        
        void insertBarrierAfterCompute(vk::CommandBuffer cmd);

        // Accessors
        vk::Buffer getDrawCommandBuffer() const { return drawCommandBuffer; }
        vk::Buffer getDrawCountBuffer() const { return drawCountBuffer; }
        vk::Buffer getPerDrawDataBuffer() const { return perDrawDataBuffer; }

        uint32_t getMaxDrawCommands() const { return maxDrawCommands; }
        
        uint32_t readBackDrawCount();
        
        GPUCullStats readBackStats();

        // Statistics
        size_t getDrawCommandBufferSize() const {
            return maxDrawCommands * sizeof(DrawIndexedIndirectCommand);
        }
        size_t getPerDrawDataBufferSize() const {
            return maxDrawCommands * sizeof(PerDrawData);
        }

    private:

        void createBuffers();
        void destroyBuffers();
    };

}
