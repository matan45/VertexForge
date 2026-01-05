#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core {
    class Device;
}

namespace render::gpudriven {

    
    class IndirectBatchManager {
    private:
        core::Device& device;
        
        vk::Buffer combinedDrawCommandBuffer;
        vk::DeviceMemory combinedDrawCommandMemory;

        vk::Buffer combinedDrawCountBuffer;
        vk::DeviceMemory combinedDrawCountMemory;

        vk::Buffer combinedPerDrawDataBuffer;
        vk::DeviceMemory combinedPerDrawDataMemory;
        
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;

        uint32_t batchCount = 0;
        uint32_t commandsPerBatch = 0;
        uint32_t shaderGroupCount = 0;
        uint32_t commandsPerSection = 0;  // commands per (batch, shaderGroup) section
        bool initialized = false;
        
    public:
        explicit IndirectBatchManager(core::Device& device);
        ~IndirectBatchManager();

        // Non-copyable
        IndirectBatchManager(const IndirectBatchManager&) = delete;
        IndirectBatchManager& operator=(const IndirectBatchManager&) = delete;
        
        bool init(uint32_t batchCount = DEFAULT_BATCH_COUNT,
                  uint32_t commandsPerBatch = MAX_DRAW_COMMANDS,
                  uint32_t shaderGroupCount = MAX_SHADER_GROUPS);
        
        bool initWithAutoConfig();
        
        void cleanup();
        
        void resetAllBatches(vk::CommandBuffer cmd);
        
        void insertBarriersAfterCompute(vk::CommandBuffer cmd);

       
        uint32_t getBatchCount() const { return batchCount; }
        uint32_t getCommandsPerBatch() const { return commandsPerBatch; }
        uint32_t getShaderGroupCount() const { return shaderGroupCount; }
        uint32_t getCommandsPerSection() const { return commandsPerSection; }
        uint32_t getTotalCapacity() const { return batchCount * shaderGroupCount * commandsPerSection; }
        uint32_t getSectionCount() const { return batchCount * shaderGroupCount; }

        vk::Buffer getCombinedDrawCommandBuffer() const { return combinedDrawCommandBuffer; }
        vk::Buffer getCombinedDrawCountBuffer() const { return combinedDrawCountBuffer; }
        vk::Buffer getCombinedPerDrawDataBuffer() const { return combinedPerDrawDataBuffer; }

        vk::DeviceSize getCombinedDrawCommandBufferSize() const {
            return batchCount * shaderGroupCount * commandsPerSection * sizeof(MeshTasksIndirectCommand);
        }
        vk::DeviceSize getCombinedDrawCountBufferSize() const {
            return batchCount * shaderGroupCount * sizeof(BatchDrawStats);
        }
        vk::DeviceSize getCombinedPerDrawDataBufferSize() const {
            return batchCount * shaderGroupCount * commandsPerSection * sizeof(PerDrawData);
        }

        uint32_t getSectionIndex(uint32_t batch, uint32_t shaderGroup) const {
            return batch * shaderGroupCount + shaderGroup;
        }
        vk::DeviceSize getDrawCommandOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * commandsPerSection * sizeof(MeshTasksIndirectCommand);
        }
        vk::DeviceSize getDrawCountOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * sizeof(BatchDrawStats);
        }

        GPUDrivenStats readBackAggregatedStats();
        
        static vk::DeviceSize calculateRequiredMemory(uint32_t batchCount,
                                                       uint32_t commandsPerBatch,
                                                       uint32_t shaderGroupCount);

    private:
        bool createBuffers();
        void destroyBuffers();
        std::vector<BatchDrawStats> readBackAllStats();
    };

}
