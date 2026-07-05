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
        dirty = true;
    }

    void VTPageTable::cleanup()
    {
        if (!initialized)
            return;

        destroyBuffers();
        cpuTable.clear();
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
        if (usedEntries + count > totalEntries)
        {
            vfLogError("VTPageTable: cannot allocate block of {} entries ({} of {} used)",
                       count, usedEntries, totalEntries);
            return VT_INVALID_TILE;
        }

        const uint32_t offset = usedEntries;
        usedEntries += count;
        std::fill(cpuTable.begin() + offset, cpuTable.begin() + offset + count, 0u);
        dirty = true;
        return offset;
    }

    void VTPageTable::mapEntry(uint32_t entryIndex, uint32_t tileX, uint32_t tileY)
    {
        if (entryIndex >= totalEntries)
            return;
        cpuTable[entryIndex] = vtPackPageEntry(tileX, tileY);
        dirty = true;
    }

    void VTPageTable::unmapEntry(uint32_t entryIndex)
    {
        if (entryIndex >= totalEntries)
            return;
        cpuTable[entryIndex] = 0u;
        dirty = true;
    }

    void VTPageTable::clearRange(uint32_t entryIndex, uint32_t count)
    {
        if (entryIndex + count > totalEntries)
            return;
        std::fill(cpuTable.begin() + entryIndex, cpuTable.begin() + entryIndex + count, 0u);
        dirty = true;
    }

    void VTPageTable::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !dirty)
            return;

        vk::DeviceSize dataSize = sizeof(uint32_t) * usedEntries;
        if (dataSize == 0)
            dataSize = sizeof(uint32_t);

        // Rotate the staging ring so an in-flight upload's source is not overwritten
        // (the defect VSMPageTable never fixed).
        currentStagingFrame = (currentStagingFrame + 1u) % core::MAX_FRAMES_IN_FLIGHT;
        auto& sf = stagingFrames[currentStagingFrame];

        std::memcpy(sf.mapped, cpuTable.data(), dataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;
        cmd.copyBuffer(sf.buffer, tableBuffer, 1, &copyRegion);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = tableBuffer;
        barrier.offset = 0;
        barrier.size = dataSize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        dirty = false;
    }

    void VTPageTable::reset()
    {
        usedEntries = 0;
        std::fill(cpuTable.begin(), cpuTable.end(), 0u);
        dirty = true;
    }
}
