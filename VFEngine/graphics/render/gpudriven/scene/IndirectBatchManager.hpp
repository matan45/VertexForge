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

    // Per-(batch, shaderGroup) occupancy array (one uint8_t per section): 1 if any CPU object
    // maps to that section, else 0. Used to skip recording empty indirect draws. Kept free of any
    // Device/GPU dependency so the section-skip logic is unit-testable on the CPU in isolation.
    //
    // Section indexing matches IndirectBatchManager::getSectionIndex():
    //   index = batch * shaderGroupCount + shaderGroup
    // Max index = (batchCount-1)*shaderGroupCount + (shaderGroupCount-1)
    //           <= MAX_BATCH_COUNT*MAX_SHADER_GROUPS - 1, so the fixed-size array always fits.
    struct SectionOccupancy {
        uint32_t batchCount = 0;
        uint32_t shaderGroupCount = 0;
        std::array<uint8_t, MAX_BATCH_COUNT * MAX_SHADER_GROUPS> occupied{};

        // Sets the grid dimensions and clears all occupancy. Out-of-range dimensions are not
        // re-clamped here — IndirectBatchManager::init() already validates them before calling.
        void configure(uint32_t batches, uint32_t groups) {
            batchCount = batches;
            shaderGroupCount = groups;
            occupied.fill(uint8_t{0});
        }

        void clear() { occupied.fill(uint8_t{0}); }

        uint32_t sectionIndex(uint32_t batch, uint32_t shaderGroup) const {
            return batch * shaderGroupCount + shaderGroup;
        }

        // Marks the section for each object as occupied. count = number of cpuObjectData
        // entries to scan (object indices map 1:1 to GPU objectIndex: identity in edit mode,
        // slot index in play mode — over-marking inactive play-mode slots is a safe superset,
        // never under-marks, so no geometry can be skipped).
        void recompute(const std::vector<GPUObjectData>& objects, uint32_t count) {
            occupied.fill(uint8_t{0});
            if (batchCount == 0 || shaderGroupCount == 0) return;
            const uint32_t n = std::min(count, static_cast<uint32_t>(objects.size()));
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t group = objects[i].shaderGroupIndex;
                if (group >= shaderGroupCount) continue;          // safety against out-of-range
                occupied[sectionIndex(i % batchCount, group)] = 1;
            }
        }

        bool hasCandidates(uint32_t batch, uint32_t shaderGroup) const {
            if (shaderGroup >= shaderGroupCount || batch >= batchCount) return false;
            return occupied[sectionIndex(batch, shaderGroup)] != 0;
        }
    };

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

        // Per-section occupancy (see SectionOccupancy above). Kept as a Device-free member so
        // the section-skip logic can be unit-tested without constructing a real batch manager.
        SectionOccupancy occupancy;

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

        // Delegates to SectionOccupancy::recompute. See that struct for the indexing/superset
        // contract. occupancy is configured with this manager's batch/shaderGroup counts in init().
        void recomputeOccupancy(const std::vector<GPUObjectData>& objects, uint32_t count) {
            occupancy.recompute(objects, count);
        }

        bool sectionHasCandidates(uint32_t batch, uint32_t shaderGroup) const {
            return occupancy.hasCandidates(batch, shaderGroup);
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
