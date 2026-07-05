#include "VTFeedbackReadback.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <algorithm>

namespace render::vt
{
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
        createBuffers();
        state = VTFeedbackState::Idle;
        initialized = true;
        vfLogInfo("VTFeedbackReadback: {} entries ({} KB)", totalEntries,
                  (sizeof(uint32_t) * totalEntries) / 1024);
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
        const vk::DeviceSize size = sizeof(uint32_t) * totalEntries;

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
            core::BufferInfoRequest request(
                logicalDevice, physicalDevice, size,
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            core::BufferUtilities::createBuffer(request, stagingBuffer, stagingAllocation, memManager);
        }
    }

    void VTFeedbackReadback::clear(vk::CommandBuffer cmd)
    {
        if (!initialized)
            return;

        cmd.fillBuffer(feedbackBuffer, 0, sizeof(uint32_t) * totalEntries, 0);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = feedbackBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * totalEntries;

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
        barrier.size = sizeof(uint32_t) * totalEntries;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, 0, nullptr, 1, &barrier, 0, nullptr);

        vk::BufferCopy copyRegion{};
        copyRegion.size = sizeof(uint32_t) * totalEntries;
        cmd.copyBuffer(feedbackBuffer, stagingBuffer, copyRegion);

        state = VTFeedbackState::Pending;
    }

    void VTFeedbackReadback::markReady()
    {
        if (state == VTFeedbackState::Pending)
            state = VTFeedbackState::Ready;
    }

    std::vector<uint32_t> VTFeedbackReadback::readback()
    {
        std::vector<uint32_t> results;
        if (state != VTFeedbackState::Ready || !initialized)
            return results;

        results.resize(totalEntries);
        std::memcpy(results.data(), stagingAllocation.mappedPtr, sizeof(uint32_t) * totalEntries);
        state = VTFeedbackState::Idle;
        return results;
    }
}
