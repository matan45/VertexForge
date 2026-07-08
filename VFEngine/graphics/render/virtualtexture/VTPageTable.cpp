#include "VTPageTable.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <algorithm>

namespace render::vt
{
    VTPageTable::VTPageTable(core::Device& device)
        : device(device)
    {
    }

    VTPageTable::~VTPageTable()
    {
        cleanup();
    }

    void VTPageTable::init(uint32_t entries)
    {
        if (initialized)
            return;

        totalEntries = entries == 0 ? 1u : entries;
        cpuTable.assign(totalEntries, 0u); // 0 == invalid (no valid bit)
        usedEntries = 0;
        currentStagingFrame = 0;

        createBuffers();

        initialized = true;
        dirtyChunks.reset(totalEntries);
        dirtyChunks.markAll(); // first upload pushes the whole (zeroed) table, as before
    }

    void VTPageTable::cleanup()
    {
        if (!initialized)
            return;

        destroyBuffers();
        cpuTable.clear();
        freeBlocks.clear();
        totalEntries = 0;
        usedEntries = 0;
        initialized = false;
    }

    void VTPageTable::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        auto& memManager = device.getMemoryManager();

        const vk::DeviceSize bufferSize = sizeof(uint32_t) * totalEntries;

        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::BufferUtilities::createBuffer(request, tableBuffer, tableAllocation, memManager);
        }

        for (auto& sf : stagingFrames)
        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, bufferSize,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            core::BufferUtilities::createBuffer(request, sf.buffer, sf.allocation, memManager);
            sf.mapped = sf.allocation.mappedPtr;
        }
    }

    void VTPageTable::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        auto& memManager = device.getMemoryManager();

        for (auto& sf : stagingFrames)
        {
            sf.mapped = nullptr;
            core::BufferUtilities::destroyBuffer(logicalDevice, sf.buffer, sf.allocation, memManager);
        }
        core::BufferUtilities::destroyBuffer(logicalDevice, tableBuffer, tableAllocation, memManager);
    }

    uint32_t VTPageTable::allocateBlock(uint32_t pagesX0, uint32_t pagesY0, uint32_t mipCount)
    {
        const uint32_t count = vtBlockEntryCount(pagesX0, pagesY0, mipCount);
        if (count == 0u)
            return VT_INVALID_TILE;

        // Reuse a same-size freed block before extending the bump pointer.
        if (auto it = freeBlocks.find(count); it != freeBlocks.end() && !it->second.empty())
        {
            const uint32_t offset = it->second.back();
            it->second.pop_back();
            std::fill(cpuTable.begin() + offset, cpuTable.begin() + offset + count, 0u);
            dirtyChunks.markRange(offset, count);
            return offset;
        }

        if (usedEntries + count > totalEntries)
        {
            vfLogError("VTPageTable: cannot allocate block of {} entries ({} of {} used)",
                       count, usedEntries, totalEntries);
            return VT_INVALID_TILE;
        }

        const uint32_t offset = usedEntries;
        usedEntries += count;
        std::fill(cpuTable.begin() + offset, cpuTable.begin() + offset + count, 0u);
        dirtyChunks.markRange(offset, count);
        return offset;
    }

    void VTPageTable::freeBlock(uint32_t base, uint32_t count)
    {
        if (count == 0u || base + count > totalEntries)
            return;
        // Zero the entries (marks them dirty so the GPU table clears the retired image's pages) and
        // return the span to its size bucket for reuse.
        clearRange(base, count);
        freeBlocks[count].push_back(base);
    }

    void VTPageTable::mapEntry(uint32_t entryIndex, uint32_t tileX, uint32_t tileY)
    {
        if (entryIndex >= totalEntries)
            return;
        cpuTable[entryIndex] = vtPackPageEntry(tileX, tileY);
        dirtyChunks.markEntry(entryIndex);
    }

    void VTPageTable::unmapEntry(uint32_t entryIndex)
    {
        if (entryIndex >= totalEntries)
            return;
        cpuTable[entryIndex] = 0u;
        dirtyChunks.markEntry(entryIndex);
    }

    void VTPageTable::clearRange(uint32_t entryIndex, uint32_t count)
    {
        if (entryIndex + count > totalEntries)
            return;
        std::fill(cpuTable.begin() + entryIndex, cpuTable.begin() + entryIndex + count, 0u);
        dirtyChunks.markRange(entryIndex, count);
    }

    void VTPageTable::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !dirtyChunks.any())
            return;

        // Rotate the staging ring so an in-flight upload's source is not overwritten
        // (the defect VSMPageTable never fixed).
        currentStagingFrame = (currentStagingFrame + 1u) % core::MAX_FRAMES_IN_FLIGHT;
        auto& sf = stagingFrames[currentStagingFrame];

        // Upload only the dirty chunks: pack their entries sequentially into the
        // staging frame (sized totalEntries*4, so disjoint ranges always fit) and
        // scatter each back to its original slot with one copyBuffer region.
        dirtyChunks.takeRanges(dirtyRanges);
        if (dirtyRanges.empty())
            return;

        std::vector<vk::BufferCopy> regions;
        regions.reserve(dirtyRanges.size());
        auto* dst = static_cast<uint8_t*>(sf.mapped);
        vk::DeviceSize packedOffset = 0;
        for (const auto& r : dirtyRanges)
        {
            const vk::DeviceSize bytes = sizeof(uint32_t) * r.entryCount;
            std::memcpy(dst + packedOffset, cpuTable.data() + r.firstEntry, static_cast<size_t>(bytes));

            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = packedOffset;
            copyRegion.dstOffset = sizeof(uint32_t) * r.firstEntry;
            copyRegion.size = bytes;
            regions.push_back(copyRegion);

            packedOffset += bytes;
        }
        cmd.copyBuffer(sf.buffer, tableBuffer, static_cast<uint32_t>(regions.size()), regions.data());

        // Whole-buffer barrier: simpler than one per region and correct for the reads.
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = tableBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * totalEntries;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {}, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    void VTPageTable::reset()
    {
        usedEntries = 0;
        freeBlocks.clear();
        std::fill(cpuTable.begin(), cpuTable.end(), 0u);
        dirtyChunks.markAll();
    }
}
