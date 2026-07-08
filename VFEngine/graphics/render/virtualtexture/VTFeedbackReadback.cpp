#include "VTFeedbackReadback.hpp"
#include "VTFeedbackWords.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <algorithm>

namespace render::vt
{
    namespace
    {
        // Choose staging memory for the feedback readback. The render thread memcpys
        // the whole staging buffer every frame, so prefer HOST_CACHED (cached reads)
        // over the plain HOST_VISIBLE|HOST_COHERENT the driver would otherwise hand
        // out — on NVIDIA that lands in uncached write-combined memory, making the
        // read stall. Never accept non-coherent memory: we don't invalidate the
        // mapped range before reading. MemoryUtilities::findMemoryType is unfit for
        // the probe — on a miss it logs an error and returns type 0 (device-local).
        vk::MemoryPropertyFlags pickStagingMemoryProperties(const vk::Device& logicalDevice,
                                                            const vk::PhysicalDevice& physicalDevice,
                                                            vk::DeviceSize size)
        {
            const vk::MemoryPropertyFlags coherent =
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            const vk::MemoryPropertyFlags cached = coherent | vk::MemoryPropertyFlagBits::eHostCached;

            // Probe an identical staging buffer to learn which memory types it permits.
            vk::BufferCreateInfo probeInfo{};
            probeInfo.size = size;
            probeInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
            probeInfo.sharingMode = vk::SharingMode::eExclusive;
            vk::Buffer probe = logicalDevice.createBuffer(probeInfo);
            const uint32_t typeBits = logicalDevice.getBufferMemoryRequirements(probe).memoryTypeBits;
            logicalDevice.destroyBuffer(probe);

            const auto memProps = physicalDevice.getMemoryProperties();
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            {
                if ((typeBits & (1u << i)) == 0u)
                    continue;
                if ((memProps.memoryTypes[i].propertyFlags & cached) == cached)
                {
                    vfLogInfo("VTFeedbackReadback: staging memory HOST_VISIBLE|HOST_COHERENT|HOST_CACHED (type {})", i);
                    return cached;
                }
            }

            vfLogInfo("VTFeedbackReadback: staging memory HOST_VISIBLE|HOST_COHERENT (no HOST_CACHED type available)");
            return coherent;
        }
    }

    VTFeedbackReadback::VTFeedbackReadback(core::Device& device)
        : device(device)
    {
    }

    VTFeedbackReadback::~VTFeedbackReadback()
    {
        cleanup();
    }

    void VTFeedbackReadback::init(uint32_t entries)
    {
        if (initialized)
            return;

        totalEntries = entries == 0 ? 1u : entries;
        wordCount = vtFeedbackWordCount(totalEntries);
        createBuffers();
        state = VTFeedbackState::Idle;
        initialized = true;
        vfLogInfo("VTFeedbackReadback: {} entries, bit-packed to {} words ({} KB)", totalEntries,
                  wordCount, (sizeof(uint32_t) * wordCount) / 1024);
    }

    void VTFeedbackReadback::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        auto& memManager = device.getMemoryManager();
        core::BufferUtilities::destroyBuffer(logicalDevice, stagingBuffer, stagingAllocation, memManager);
        core::BufferUtilities::destroyBuffer(logicalDevice, feedbackBuffer, feedbackAllocation, memManager);

        initialized = false;
    }

    void VTFeedbackReadback::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();
        auto& memManager = device.getMemoryManager();
        const vk::DeviceSize size = sizeof(uint32_t) * wordCount;

        {
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, size,
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferSrc |
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            core::BufferUtilities::createBuffer(request, feedbackBuffer, feedbackAllocation, memManager);
        }
        {
            const vk::MemoryPropertyFlags stagingProps =
                pickStagingMemoryProperties(logicalDevice, physicalDevice, size);
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, size,
                vk::BufferUsageFlagBits::eTransferDst,
                stagingProps);
            core::BufferUtilities::createBuffer(request, stagingBuffer, stagingAllocation, memManager);
        }
    }

    void VTFeedbackReadback::clear(vk::CommandBuffer cmd)
    {
        if (!initialized)
            return;

        cmd.fillBuffer(feedbackBuffer, 0, sizeof(uint32_t) * wordCount, 0);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = feedbackBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * wordCount;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, 0, nullptr, 1, &barrier, 0, nullptr);
    }

    void VTFeedbackReadback::copyToStaging(vk::CommandBuffer cmd)
    {
        if (!initialized)
            return;

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = feedbackBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * wordCount;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        vk::BufferCopy copyRegion{};
        copyRegion.size = sizeof(uint32_t) * wordCount;
        cmd.copyBuffer(feedbackBuffer, stagingBuffer, copyRegion);

        state = VTFeedbackState::Pending;
    }

    void VTFeedbackReadback::markReady()
    {
        if (state == VTFeedbackState::Pending)
            state = VTFeedbackState::Ready;
    }

    const std::vector<uint32_t>& VTFeedbackReadback::readback()
    {
        if (state != VTFeedbackState::Ready || !initialized)
        {
            resultsBuffer.clear(); // keeps capacity; returns empty when nothing is ready
            return resultsBuffer;
        }

        resultsBuffer.resize(wordCount); // no realloc after the first warm frame (wordCount is fixed)
        std::memcpy(resultsBuffer.data(), stagingAllocation.mappedPtr, sizeof(uint32_t) * wordCount);
        state = VTFeedbackState::Idle;
        return resultsBuffer;
    }
}
