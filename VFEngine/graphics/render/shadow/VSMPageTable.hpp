#pragma once

#include "VSMTypes.hpp"
#include "../../core/RenderManager.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
}

namespace render::shadow
{
    class VSMPageTable
    {
    private:
        core::Device& device;

        // GPU SSBO holding page table entries
        vk::Buffer pageTableBuffer;
        vk::DeviceMemory pageTableMemory;

        struct PageTableStagingFrame
        {
            vk::Buffer buffer;
            vk::DeviceMemory memory;
            void* mapped = nullptr;
        };

        std::array<PageTableStagingFrame, core::MAX_FRAMES_IN_FLIGHT> stagingFrames{};
        uint32_t currentStagingFrame = 0;

        // CPU mirror
        std::vector<uint32_t> cpuPageTable;
        uint32_t totalEntries = 0;

        // Next available offset for light allocation
        uint32_t nextPageTableOffset = 0;

        bool initialized = false;
        bool dirty = true;

    public:
        explicit VSMPageTable(core::Device& device);
        ~VSMPageTable();

        VSMPageTable(const VSMPageTable&) = delete;
        VSMPageTable& operator=(const VSMPageTable&) = delete;

        void init();
        void cleanup();

        // Allocate a contiguous block of page table entries for a light
        // Returns the offset in the page table buffer
        [[nodiscard]] uint32_t allocateBlock(uint32_t pagesX, uint32_t pagesY);
        void freeBlock(uint32_t offset, uint32_t pagesX, uint32_t pagesY);

        void mapPage(uint32_t offset, uint32_t pageX, uint32_t pageY, uint32_t pagesX, uint32_t physicalTileIndex);
        void unmapPage(uint32_t offset, uint32_t pageX, uint32_t pageY, uint32_t pagesX);
        void clearBlock(uint32_t offset, uint32_t count);

        void uploadToGPU(vk::CommandBuffer cmd);

        [[nodiscard]] vk::Buffer getBuffer() const { return pageTableBuffer; }
        [[nodiscard]] vk::DeviceSize getBufferSize() const;
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] bool isDirty() const { return dirty; }

        void reset();

    private:
        void createBuffers();
        void destroyBuffers();
    };
}
