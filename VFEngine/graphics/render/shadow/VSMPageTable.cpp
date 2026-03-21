#include "VSMPageTable.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    VSMPageTable::VSMPageTable(core::Device& device)
        : device(device)
    {
    }

    VSMPageTable::~VSMPageTable()
    {
        cleanup();
    }

    void VSMPageTable::init()
    {
        if (initialized)
            return;

        // Max entries: MAX_VSM_LIGHTS * PAGES_PER_SIDE * PAGES_PER_SIDE
        // For directional: 128*128 = 16384 pages per light
        // Total: 64 * 16384 = 1048576 entries (4MB)
        totalEntries = vsm::MAX_VSM_LIGHTS * vsm::PAGES_PER_SIDE * vsm::PAGES_PER_SIDE;
        cpuPageTable.resize(totalEntries, vsm::INVALID_TILE);

        createBuffers();

        initialized = true;
        nextPageTableOffset = 0;
    }

    void VSMPageTable::cleanup()
    {
        if (!initialized)
            return;

        destroyBuffers();
        cpuPageTable.clear();
        totalEntries = 0;
        nextPageTableOffset = 0;
        initialized = false;
    }

    void VSMPageTable::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = sizeof(uint32_t) * totalEntries;

        // Device-local buffer
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::BufferUtilities::createBuffer(request, pageTableBuffer, pageTableMemory);
        }

        // Per-frame staging buffers
        for (auto& sf : stagingFrames)
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, sf.buffer, sf.memory);
            sf.mapped = logicalDevice.mapMemory(sf.memory, 0, bufferSize);
        }
    }

    void VSMPageTable::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& sf : stagingFrames)
        {
            if (sf.mapped) { logicalDevice.unmapMemory(sf.memory); sf.mapped = nullptr; }
            if (sf.buffer) { logicalDevice.destroyBuffer(sf.buffer); sf.buffer = nullptr; }
            if (sf.memory) { logicalDevice.freeMemory(sf.memory); sf.memory = nullptr; }
        }

        if (pageTableBuffer)
        {
            logicalDevice.destroyBuffer(pageTableBuffer);
            pageTableBuffer = nullptr;
        }
        if (pageTableMemory)
        {
            logicalDevice.freeMemory(pageTableMemory);
            pageTableMemory = nullptr;
        }
    }

    uint32_t VSMPageTable::allocateBlock(uint32_t pagesX, uint32_t pagesY)
    {
        uint32_t count = pagesX * pagesY;
        if (nextPageTableOffset + count > totalEntries)
        {
            vfLogError("VSMPageTable: Cannot allocate block of {} entries, only {} remaining",
                       count, totalEntries - nextPageTableOffset);
            return vsm::INVALID_TILE;
        }

        uint32_t offset = nextPageTableOffset;
        nextPageTableOffset += count;

        // Initialize all entries as invalid
        for (uint32_t i = 0; i < count; ++i)
            cpuPageTable[offset + i] = 0; // invalid (no valid bit set)

        dirty = true;
        return offset;
    }

    void VSMPageTable::freeBlock(uint32_t offset, uint32_t pagesX, uint32_t pagesY)
    {
        uint32_t count = pagesX * pagesY;
        if (offset + count > totalEntries)
            return;

        for (uint32_t i = 0; i < count; ++i)
            cpuPageTable[offset + i] = 0;

        dirty = true;
    }

    void VSMPageTable::mapPage(uint32_t offset, uint32_t pageX, uint32_t pageY, uint32_t pagesX, uint32_t physicalTileIndex)
    {
        uint32_t entryIndex = offset + pageY * pagesX + pageX;
        if (entryIndex >= totalEntries)
            return;

        uint32_t tileX = physicalTileIndex % vsm::TILES_PER_SIDE;
        uint32_t tileY = physicalTileIndex / vsm::TILES_PER_SIDE;

        cpuPageTable[entryIndex] = vsm::packPageEntry(tileX, tileY);
        dirty = true;
    }

    void VSMPageTable::unmapPage(uint32_t offset, uint32_t pageX, uint32_t pageY, uint32_t pagesX)
    {
        uint32_t entryIndex = offset + pageY * pagesX + pageX;
        if (entryIndex >= totalEntries)
            return;

        cpuPageTable[entryIndex] = 0; // clear valid bit
        dirty = true;
    }

    void VSMPageTable::clearBlock(uint32_t offset, uint32_t count)
    {
        if (offset + count > totalEntries)
            return;

        for (uint32_t i = 0; i < count; ++i)
            cpuPageTable[offset + i] = 0;

        dirty = true;
    }

    void VSMPageTable::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !dirty)
            return;

        // Only upload the used portion
        vk::DeviceSize dataSize = sizeof(uint32_t) * nextPageTableOffset;
        if (dataSize == 0)
            dataSize = sizeof(uint32_t); // at least one entry

        auto& sf = stagingFrames[currentStagingFrame];

        std::memcpy(sf.mapped, cpuPageTable.data(), dataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;

        cmd.copyBuffer(sf.buffer, pageTableBuffer, 1, &copyRegion);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = pageTableBuffer;
        barrier.offset = 0;
        barrier.size = dataSize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {},
            0, nullptr,
            1, &barrier,
            0, nullptr
        );

        dirty = false;
    }

    vk::DeviceSize VSMPageTable::getBufferSize() const
    {
        return sizeof(uint32_t) * totalEntries;
    }

    void VSMPageTable::reset()
    {
        nextPageTableOffset = 0;
        std::fill(cpuPageTable.begin(), cpuPageTable.end(), 0u);
        dirty = true;
    }
}
