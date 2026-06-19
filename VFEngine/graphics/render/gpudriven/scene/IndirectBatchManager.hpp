#pragma once

#include "../GPUDrivenTypes.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <array>
#include <algorithm>

namespace core {
    class Device;
}

namespace render::gpudriven {

    
    class IndirectBatchManager {
    private:
        core::Device& device;
        
        vk::Buffer combinedDrawCommandBuffer;
        core::VulkanAllocation combinedDrawCommandAllocation;

        vk::Buffer combinedDrawCountBuffer;
        core::VulkanAllocation combinedDrawCountAllocation;

        vk::Buffer combinedPerDrawDataBuffer;
        core::VulkanAllocation combinedPerDrawDataAllocation;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;
        void* stagingMapped = nullptr;

        uint32_t batchCount = 0;
        uint32_t commandsPerBatch = 0;
        uint32_t shaderGroupCount = 0;
        uint32_t commandsPerSection = 0;  // commands per (batch, shaderGroup) section
        bool initialized = false;

        // Per-section occupancy: 1 if any CPU object maps to that (batch, shaderGroup)
        // section, else 0. Used to skip recording empty indirect draws. Indexed via
        // getSectionIndex(): max index = (batchCount-1)*shaderGroupCount + (shaderGroupCount-1)
        // <= MAX_BATCH_COUNT*MAX_SHADER_GROUPS - 1, so the fixed-size array always fits.
        std::array<uint8_t, MAX_BATCH_COUNT * MAX_SHADER_GROUPS> sectionOccupied{};

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

        uint32_t getSectionIndex(uint32_t batch, uint32_t shaderGroup) const {
            return batch * shaderGroupCount + shaderGroup;
        }

        void clearOccupancy() { sectionOccupied.fill(uint8_t{0}); }

        // Marks the section for each object as occupied. count = number of cpuObjectData
        // entries to scan (object indices map 1:1 to GPU objectIndex: identity in edit mode,
        // slot index in play mode — over-marking inactive play-mode slots is a safe superset,
        // never under-marks, so no geometry can be skipped).
        void recomputeOccupancy(const std::vector<GPUObjectData>& objects, uint32_t count) {
            sectionOccupied.fill(uint8_t{0});
            if (batchCount == 0 || shaderGroupCount == 0) return;
            const uint32_t n = std::min(count, static_cast<uint32_t>(objects.size()));
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t group = objects[i].shaderGroupIndex;
                if (group >= shaderGroupCount) continue;          // safety against out-of-range
                sectionOccupied[getSectionIndex(i % batchCount, group)] = 1;
            }
        }

        bool sectionHasCandidates(uint32_t batch, uint32_t shaderGroup) const {
            if (shaderGroup >= shaderGroupCount || batch >= batchCount) return false;
            return sectionOccupied[getSectionIndex(batch, shaderGroup)] != 0;
        }

        vk::DeviceSize getDrawCommandOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * commandsPerSection * sizeof(MeshTasksIndirectCommand);
        }

        vk::DeviceSize getDrawCountOffset(uint32_t batch, uint32_t shaderGroup) const {
            return getSectionIndex(batch, shaderGroup) * sizeof(BatchDrawStats);
        }

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
