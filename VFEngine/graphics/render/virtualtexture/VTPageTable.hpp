#pragma once

#include "VTTypes.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

// ============================================================================
// Virtual Texturing (VK-1209) — GPU page table: a device-local SSBO of packed
// entries (VTTypes) + a CPU mirror uploaded on change. Copy-adapted from
// VSMPageTable, with three changes: (1) blocks are full mip pyramids (per virtual
// image) rather than one flat grid per light; (2) the staging ring ACTUALLY
// rotates each upload (VSMPageTable.cpp:173 leaves currentStagingFrame at 0 — a
// latent hazard we do not copy); (3) entries encode 12+12 tile bits.
// ============================================================================

namespace render::vt
{
    class VTPageTable
    {
    public:
        explicit VTPageTable(core::Device& device);
        ~VTPageTable();

        VTPageTable(const VTPageTable&) = delete;
        VTPageTable& operator=(const VTPageTable&) = delete;

        void init(uint32_t totalEntries);
        void cleanup();

        // Reserve a contiguous mip-pyramid block for a virtual image; returns its base
        // offset in the global table (VT_INVALID_TILE on overflow).
        [[nodiscard]] uint32_t allocateBlock(uint32_t pagesX0, uint32_t pagesY0, uint32_t mipCount);

        // entryIndex is a GLOBAL index (block base + vtPageLinearIndex(...)).
        void mapEntry(uint32_t entryIndex, uint32_t tileX, uint32_t tileY);
        void unmapEntry(uint32_t entryIndex);
        void clearRange(uint32_t entryIndex, uint32_t count);

        void uploadToGPU(vk::CommandBuffer cmd);
        void reset();

        [[nodiscard]] vk::Buffer getBuffer() const { return tableBuffer; }
        [[nodiscard]] vk::DeviceSize getBufferSize() const { return sizeof(uint32_t) * totalEntries; }
        [[nodiscard]] uint32_t getTotalEntries() const { return totalEntries; }
        [[nodiscard]] const std::vector<uint32_t>& cpuMirror() const { return cpuTable; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createBuffers();
        void destroyBuffers();

        struct StagingFrame
        {
            vk::Buffer buffer;
            core::VulkanAllocation allocation;
            void* mapped = nullptr;
        };

        core::Device& device;

        vk::Buffer tableBuffer;
        core::VulkanAllocation tableAllocation;

        std::array<StagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        std::vector<uint32_t> cpuTable;
        uint32_t totalEntries = 0;
        uint32_t usedEntries = 0; // bump pointer / high-water mark for upload size

        bool initialized = false;
        bool dirty = true;
    };
}
