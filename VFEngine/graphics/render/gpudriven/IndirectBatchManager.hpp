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
         * Initialize with specified batch count and commands per batch.
         * @param batchCount Number of batches (1-8, default 4)
         * @param commandsPerBatch Max draw commands per batch (default MAX_DRAW_COMMANDS)
         */
        void init(uint32_t batchCount = DEFAULT_BATCH_COUNT,
                  uint32_t commandsPerBatch = MAX_DRAW_COMMANDS);

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
        uint32_t getTotalCapacity() const { return batchCount * commandsPerBatch; }
        bool isInitialized() const { return initialized; }

        // Combined buffer accessors (for compute shader bindings)
        vk::Buffer getCombinedDrawCommandBuffer() const { return combinedDrawCommandBuffer; }
        vk::Buffer getCombinedDrawCountBuffer() const { return combinedDrawCountBuffer; }
        vk::Buffer getCombinedPerDrawDataBuffer() const { return combinedPerDrawDataBuffer; }

        // Buffer sizes
        vk::DeviceSize getCombinedDrawCommandBufferSize() const {
            return batchCount * commandsPerBatch * sizeof(DrawIndexedIndirectCommand);
        }
        vk::DeviceSize getCombinedDrawCountBufferSize() const {
            return batchCount * sizeof(BatchDrawStats);
        }
        vk::DeviceSize getCombinedPerDrawDataBufferSize() const {
            return batchCount * commandsPerBatch * sizeof(PerDrawData);
        }

        // Per-batch offset calculations (for multi-draw loop)
        vk::DeviceSize getDrawCommandOffset(uint32_t batch) const {
            return batch * commandsPerBatch * sizeof(DrawIndexedIndirectCommand);
        }
        vk::DeviceSize getDrawCountOffset(uint32_t batch) const {
            return batch * sizeof(BatchDrawStats);
        }
        vk::DeviceSize getPerDrawDataOffset(uint32_t batch) const {
            return batch * commandsPerBatch * sizeof(PerDrawData);
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
        bool initialized = false;

        void createBuffers();
        void destroyBuffers();
    };

}
