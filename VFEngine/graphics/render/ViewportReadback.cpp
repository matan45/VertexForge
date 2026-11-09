#include "ViewportReadback.hpp"
#include "../core/Device.hpp"
#include "../core/BufferUtilities.hpp"
#include "print/Log.hpp"

#include <cstring>
#include <exception>
#include <utility>

namespace render
{
    ViewportReadback::ViewportReadback(core::Device& device)
        : device(device)
    {
    }

    void ViewportReadback::setAvailable(bool isAvailable)
    {
        std::lock_guard lock(mutex);
        available = isAvailable;
    }

    uint64_t ViewportReadback::request(uint32_t settleFrames)
    {
        std::lock_guard lock(mutex);

        // Replaces whatever was pending: an older Ready result is dropped, and a copy
        // already in flight is left to finish and then discarded (its ticket no longer
        // matches currentTicket).
        currentTicket = nextTicket++;
        readyPixels.clear();
        readyPixels.shrink_to_fit();
        readyWidth = 0;
        readyHeight = 0;

        if (available)
        {
            stage = Stage::Requested;
            framesUntilArmed = settleFrames;
        }
        else
        {
            stage = Stage::Failed;
        }

        updateBusyLocked();
        return currentTicket;
    }

    services::ViewportReadbackResult ViewportReadback::take(uint64_t ticket)
    {
        services::ViewportReadbackResult result;

        std::lock_guard lock(mutex);
        if (ticket == 0 || ticket != currentTicket)
        {
            result.state = services::ViewportReadbackResult::State::Failed;
            return result;
        }

        switch (stage)
        {
        case Stage::Requested:
        case Stage::Recorded:
            result.state = services::ViewportReadbackResult::State::Pending;
            break;
        case Stage::Ready:
            result.state = services::ViewportReadbackResult::State::Ready;
            result.width = readyWidth;
            result.height = readyHeight;
            result.rgba16f = std::move(readyPixels);
            readyPixels = {};
            readyWidth = 0;
            readyHeight = 0;
            // Consumed: a second take of this ticket reads Failed.
            currentTicket = 0;
            stage = Stage::Idle;
            break;
        case Stage::Idle:
        case Stage::Failed:
            result.state = services::ViewportReadbackResult::State::Failed;
            break;
        }

        return result;
    }

    void ViewportReadback::onFenceWaited(uint32_t frameSlot)
    {
        if (!busy.load(std::memory_order_acquire))
            return;

        std::lock_guard lock(mutex);
        if (inFlight.buffer && inFlight.slot == frameSlot)
            finishInFlightLocked();
    }

    void ViewportReadback::recordIfArmed(vk::CommandBuffer commandBuffer, vk::Image image,
                                         vk::ImageLayout currentLayout, vk::Extent2D extent, uint32_t frameSlot)
    {
        if (!busy.load(std::memory_order_acquire))
            return;

        std::lock_guard lock(mutex);

        // One buffer at a time: a replaced request's copy must reach its fence first.
        if (stage != Stage::Requested || inFlight.buffer)
            return;

        if (framesUntilArmed > 0)
        {
            --framesUntilArmed;
            return;
        }

        if (!image || extent.width == 0 || extent.height == 0)
        {
            vfLogWarning("ViewportReadback: no viewport image to read back ({}x{})", extent.width, extent.height);
            stage = Stage::Failed;
            updateBusyLocked();
            return;
        }

        // RGBA16F = 8 bytes per pixel, tightly packed rows.
        const vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(extent.width) * extent.height * 8;

        InFlightCopy copy;
        copy.ticket = currentTicket;
        copy.slot = frameSlot;
        copy.width = extent.width;
        copy.height = extent.height;

        try
        {
            core::BufferInfoRequest req(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                bufferSize,
                vk::BufferUsageFlagBits::eTransferDst,
                pickReadbackMemoryProperties(bufferSize));

            core::BufferUtilities::createBuffer(req, copy.buffer, copy.allocation, device.getMemoryManager());
        }
        catch (const std::exception& e)
        {
            vfLogError("ViewportReadback: failed to create a {} byte readback buffer: {}", bufferSize, e.what());
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), copy.buffer, copy.allocation, device.getMemoryManager());
            stage = Stage::Failed;
            updateBusyLocked();
            return;
        }

        if (!copy.buffer || !copy.allocation.isValid() || !copy.allocation.mappedPtr)
        {
            vfLogError("ViewportReadback: readback buffer is not host-mapped");
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), copy.buffer, copy.allocation, device.getMemoryManager());
            stage = Stage::Failed;
            updateBusyLocked();
            return;
        }

        const vk::ImageSubresourceRange colorRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

        // Whatever wrote the image last this frame (UI overlays, plugin overlay hooks,
        // the post-upscale composite) -> transfer read. eAllCommands/eMemoryWrite keeps
        // this independent of which pass ran last; it is a once-per-request barrier.
        vk::ImageMemoryBarrier toTransfer{};
        toTransfer.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
        toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        toTransfer.oldLayout = currentLayout;
        toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange = colorRange;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, toTransfer);

        vk::BufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;   // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{extent.width, extent.height, 1};

        commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, copy.buffer, region);

        // Back to exactly the layout the frame left it in: the next frame's graph import
        // and the ImGui viewport sample both rely on it. eAllCommands as the destination
        // chains this behind every later barrier/consumer of the image.
        vk::ImageMemoryBarrier toOriginal = toTransfer;
        toOriginal.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        toOriginal.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
        toOriginal.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        toOriginal.newLayout = currentLayout;

        // A fence wait alone does not make device writes visible to the host.
        vk::BufferMemoryBarrier toHost{};
        toHost.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        toHost.dstAccessMask = vk::AccessFlagBits::eHostRead;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = copy.buffer;
        toHost.offset = 0;
        toHost.size = VK_WHOLE_SIZE;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAllCommands | vk::PipelineStageFlagBits::eHost,
            {}, {}, toHost, toOriginal);

        inFlight = copy;
        stage = Stage::Recorded;
        updateBusyLocked();
    }

    void ViewportReadback::onRecreate()
    {
        std::lock_guard lock(mutex);
        // The caller already waited for the device to go idle, so the recorded copy is done.
        if (inFlight.buffer)
            finishInFlightLocked();
    }

    void ViewportReadback::cleanUp()
    {
        std::lock_guard lock(mutex);
        destroyInFlightLocked();
        if (stage == Stage::Requested || stage == Stage::Recorded)
            stage = Stage::Failed;
        available = false;
        updateBusyLocked();
    }

    void ViewportReadback::finishInFlightLocked()
    {
        // Only the copy for the live request is kept; a replaced request's copy is dropped.
        if (inFlight.ticket == currentTicket && stage == Stage::Recorded)
        {
            const size_t halfCount = static_cast<size_t>(inFlight.width) * inFlight.height * 4;
            readyPixels.resize(halfCount);
            std::memcpy(readyPixels.data(), inFlight.allocation.mappedPtr, halfCount * sizeof(uint16_t));
            readyWidth = inFlight.width;
            readyHeight = inFlight.height;
            stage = Stage::Ready;
        }

        destroyInFlightLocked();
        updateBusyLocked();
    }

    void ViewportReadback::destroyInFlightLocked()
    {
        if (inFlight.buffer || inFlight.allocation.isValid())
        {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), inFlight.buffer, inFlight.allocation,
                                                 device.getMemoryManager());
        }
        inFlight = {};
    }

    void ViewportReadback::updateBusyLocked()
    {
        busy.store(stage == Stage::Requested || static_cast<bool>(inFlight.buffer), std::memory_order_release);
    }

    vk::MemoryPropertyFlags ViewportReadback::pickReadbackMemoryProperties(vk::DeviceSize size) const
    {
        // Same probe as VTFeedbackReadback: prefer HOST_CACHED so the render-thread memcpy
        // of a full viewport does not read uncached write-combined memory, but never accept
        // non-coherent memory — the mapped range is not invalidated before reading.
        const vk::MemoryPropertyFlags coherent =
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        const vk::MemoryPropertyFlags cached = coherent | vk::MemoryPropertyFlagBits::eHostCached;

        const vk::Device& logicalDevice = device.getLogicalDevice();

        vk::BufferCreateInfo probeInfo{};
        probeInfo.size = size;
        probeInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
        probeInfo.sharingMode = vk::SharingMode::eExclusive;
        vk::Buffer probe = logicalDevice.createBuffer(probeInfo);
        const uint32_t typeBits = logicalDevice.getBufferMemoryRequirements(probe).memoryTypeBits;
        logicalDevice.destroyBuffer(probe);

        const auto memProps = device.getPhysicalDevice().getMemoryProperties();
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeBits & (1u << i)) == 0u)
                continue;
            if ((memProps.memoryTypes[i].propertyFlags & cached) == cached)
                return cached;
        }
        return coherent;
    }
}
