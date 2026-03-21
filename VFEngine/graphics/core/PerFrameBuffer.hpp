#pragma once

#include "GraphicsConstants.hpp"
#include "BufferUtilities.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <cstring>

namespace core
{
    /// Utility for double-buffered (MAX_FRAMES_IN_FLIGHT) host-visible GPU buffers.
    /// Handles creation, destruction, frame rotation, and mapped memory writes.
    /// Use for small per-frame UBOs/SSBOs (camera params, settings, etc.).
    class PerFrameBuffer
    {
    public:
        PerFrameBuffer() = default;
        ~PerFrameBuffer() = default;

        PerFrameBuffer(const PerFrameBuffer&) = delete;
        PerFrameBuffer& operator=(const PerFrameBuffer&) = delete;

        /// Create MAX_FRAMES_IN_FLIGHT buffers with the given size and usage.
        void create(const vk::Device& logicalDevice, const vk::PhysicalDevice& physicalDevice,
                    vk::DeviceSize size, vk::BufferUsageFlagBits usage = vk::BufferUsageFlagBits::eUniformBuffer)
        {
            bufferSize = size;
            for (auto& f : frames)
            {
                BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = size;
                request.usage = usage;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;
                BufferUtilities::createBuffer(request, f.buffer, f.memory);
                f.mapped = logicalDevice.mapMemory(f.memory, 0, size, vk::MemoryMapFlags{});
            }
        }

        /// Create MAX_FRAMES_IN_FLIGHT staging buffers (TransferSrc) for use with device-local buffers.
        void createStaging(const vk::Device& logicalDevice, const vk::PhysicalDevice& physicalDevice,
                           vk::DeviceSize size)
        {
            bufferSize = size;
            for (auto& f : frames)
            {
                BufferInfoRequest request(logicalDevice, physicalDevice);
                request.size = size;
                request.usage = vk::BufferUsageFlagBits::eTransferSrc;
                request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;
                BufferUtilities::createBuffer(request, f.buffer, f.memory);
                f.mapped = logicalDevice.mapMemory(f.memory, 0, size, vk::MemoryMapFlags{});
            }
        }

        /// Destroy all buffers.
        void destroy(const vk::Device& logicalDevice)
        {
            for (auto& f : frames)
            {
                if (f.mapped)
                {
                    logicalDevice.unmapMemory(f.memory);
                    f.mapped = nullptr;
                }
                BufferUtilities::destroyBuffer(logicalDevice, f.buffer, f.memory);
            }
        }

        /// Write data to the current frame's buffer via mapped memory.
        void write(const void* data, vk::DeviceSize size)
        {
            if (frames[currentFrame].mapped)
            {
                std::memcpy(frames[currentFrame].mapped, data, static_cast<size_t>(size));
            }
        }

        /// Write data at an offset in the current frame's buffer.
        void writeAt(const void* data, vk::DeviceSize offset, vk::DeviceSize size)
        {
            if (frames[currentFrame].mapped)
            {
                std::memcpy(static_cast<char*>(frames[currentFrame].mapped) + offset,
                           data, static_cast<size_t>(size));
            }
        }

        /// Advance to the next frame. Call once per frame before writing.
        void advance() { currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

        /// Get current frame's buffer.
        vk::Buffer getBuffer() const { return frames[currentFrame].buffer; }

        /// Get buffer for a specific frame slot.
        vk::Buffer getBuffer(uint32_t slot) const { return frames[slot % MAX_FRAMES_IN_FLIGHT].buffer; }

        /// Get current frame's mapped pointer.
        void* getMapped() const { return frames[currentFrame].mapped; }

        /// Get current frame index.
        uint32_t getCurrentFrame() const { return currentFrame; }

        vk::DeviceSize getSize() const { return bufferSize; }

    private:
        struct Frame
        {
            vk::Buffer buffer;
            vk::DeviceMemory memory;
            void* mapped = nullptr;
        };

        std::array<Frame, MAX_FRAMES_IN_FLIGHT> frames{};
        uint32_t currentFrame = 0;
        vk::DeviceSize bufferSize = 0;
    };
}
