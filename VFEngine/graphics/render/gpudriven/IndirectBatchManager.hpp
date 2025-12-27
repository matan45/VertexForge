#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core {
    class Device;
}

namespace render::gpudriven {

    /**
     * Manages multiple indirect draw batches for GPU-driven rendering.
     *
     * When scene object count exceeds MAX_DRAW_COMMANDS, objects are distributed
     * across multiple batches using round-robin assignment. Each batch has its own
     * draw commands, draw count, and per-draw data in a single combined buffer.
     *
     * Memory layout (combined buffers):
     * - Draw commands: [Batch0 commands][Batch1 commands]...[BatchN commands]
     * - Draw counts: [Batch0 stats][Batch1 stats]...[BatchN stats]
     * - Per-draw data: [Batch0 data][Batch1 data]...[BatchN data]
     */
    class IndirectBatchManager {
    public:
        explicit IndirectBatchManager(core::Device& device);
        ~IndirectBatchManager();

        // Non-copyable
        IndirectBatchManager(const IndirectBatchManager&) = delete;
        IndirectBatchManager& operator=(const IndirectBatchManager&) = delete;

        /**
         * Initialize with specified batch count, commands per batch, and shader groups.
         * @param batchCount Number of batches (1-8, default 4)
         * @param commandsPerBatch Max draw commands per batch (default MAX_DRAW_COMMANDS)
         * @param shaderGroupCount Number of shader groups for multi-pipeline rendering (default MAX_SHADER_GROUPS)
         */
        void init(uint32_t batchCount = DEFAULT_BATCH_COUNT,
                  uint32_t commandsPerBatch = MAX_DRAW_COMMANDS,
                  uint32_t shaderGroupCount = MAX_SHADER_GROUPS);

        /**
         * Cleanup all GPU resources.
         */
        void cleanup();

        /**
         * Reset all batch draw counts to 0 (called at start of frame before culling).
         * @param cmd Command buffer to record reset commands
         */
        void resetAllBatches(vk::CommandBuffer cmd);

        /**
         * Insert barriers after compute shader writes, before indirect draw.
         * @param cmd Command buffer to record barrier commands
         */
        void insertBarriersAfterCompute(vk::CommandBuffer cmd);

        // Configuration accessors
        uint32_t getBatchCount() const { return batchCount; }
        uint32_t getCommandsPerBatch() const { return commandsPerBatch; }
        uint32_t getShaderGroupCount() const { return shaderGroupCount; }
        uint32_t getCommandsPerSection() const { return commandsPerSection; }
        uint32_t getTotalCapacity() const { return batchCount * shaderGroupCount * commandsPerSection; }
        uint32_t getSectionCount() const { return batchCount * shaderGroupCount; }
        bool isInitialized() const { return initialized; }

        // Combined buffer accessors (for compute shader bindings)
        vk::Buffer getCombinedDrawCommandBuffer() const { return combinedDrawCommandBuffer; }
        vk::Buffer getCombinedDrawCountBuffer() const { return combinedDrawCountBuffer; }
        vk::Buffer getCombinedPerDrawDataBuffer() const { return combinedPerDrawDataBuffer; }

        // Buffer sizes (now account for shader groups)
        vk::DeviceSize getCombinedDrawCommandBufferSize() const {
            return batchCount * shaderGroupCount * commandsPerSection * sizeof(DrawIndexedIndirectCommand);
        }
        vk::DeviceSize getCombinedDrawCountBufferSize() const {
            return batchCount * shaderGroupCount * sizeof(BatchDrawStats);
        }
        vk::DeviceSize getCombinedPerDrawDataBufferSize() const {
            return batchCount * shaderGroupCount * commandsPerSection * sizeof(PerDrawData);
        }

        // Per-section offset calculations (batch, shaderGroup)
        // Section index = batch * shaderGroupCount + shaderGroup
        uint32_t getSectionIndex(uint32_t batch, uint32_t shaderGroup) const {
            return batch * shaderGroupCount + shaderGroup;
        }
        vk::DeviceSize getDrawCommandOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * commandsPerSection * sizeof(DrawIndexedIndirectCommand);
        }
        vk::DeviceSize getDrawCountOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * sizeof(BatchDrawStats);
        }
        vk::DeviceSize getPerDrawDataOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * commandsPerSection * sizeof(PerDrawData);
        }

        // Legacy per-batch offsets (for backward compatibility, uses group 0)
        vk::DeviceSize getDrawCommandOffset(uint32_t batch) const {
            return getDrawCommandOffset(batch, 0);
        }
        vk::DeviceSize getDrawCountOffset(uint32_t batch) const {
            return getDrawCountOffset(batch, 0);
        }
        vk::DeviceSize getPerDrawDataOffset(uint32_t batch) const {
            return getPerDrawDataOffset(batch, 0);
        }

        /**
         * Read back stats from all batches (expensive - causes GPU-CPU sync).
         * Use for debugging only.
         * @return Vector of BatchDrawStats for each batch
         */
        std::vector<BatchDrawStats> readBackAllStats();

        /**
         * Read back and aggregate stats from all batches.
         * @return Aggregated GPUDrivenStats
         */
        GPUDrivenStats readBackAggregatedStats();

    private:
        core::Device& device;

        // Combined buffers (all batches contiguous)
        vk::Buffer combinedDrawCommandBuffer;
        vk::DeviceMemory combinedDrawCommandMemory;

        vk::Buffer combinedDrawCountBuffer;
        vk::DeviceMemory combinedDrawCountMemory;

        vk::Buffer combinedPerDrawDataBuffer;
        vk::DeviceMemory combinedPerDrawDataMemory;

        // Staging buffer for reset and readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;

        uint32_t batchCount = 0;
        uint32_t commandsPerBatch = 0;
        uint32_t shaderGroupCount = 0;
        uint32_t commandsPerSection = 0;  // commands per (batch, shaderGroup) section
        bool initialized = false;

        void createBuffers();
        void destroyBuffers();
    };

}
