#pragma once

// DEPRECATED: This class uses raw vk::DeviceMemory map/unmap. New code should use
// VulkanAllocation::mappedPtr from VulkanMemoryManager instead. Retained for
// existing callers (e.g., TerrainRaycastPipeline) until they are migrated.

#include "Device.hpp"

namespace render
{
    class [[deprecated("Use VulkanAllocation::mappedPtr instead of manual map/unmap")]] MappedMemoryGuard
    {
    public:
        MappedMemoryGuard(vk::Device device, vk::DeviceMemory memory, vk::DeviceSize offset, vk::DeviceSize size)
            : device(device), memory(memory)
        {
            mappedData = device.mapMemory(memory, offset, size);
        }

        ~MappedMemoryGuard()
        {
            if (mappedData)
            {
                device.unmapMemory(memory);
            }
        }

        MappedMemoryGuard(const MappedMemoryGuard&) = delete;
        MappedMemoryGuard& operator=(const MappedMemoryGuard&) = delete;

        MappedMemoryGuard(MappedMemoryGuard&& other) noexcept
            : device(other.device), memory(other.memory), mappedData(other.mappedData)
        {
            other.mappedData = nullptr;
        }

        MappedMemoryGuard& operator=(MappedMemoryGuard&& other) noexcept
        {
            if (this != &other)
            {
                if (mappedData)
                {
                    device.unmapMemory(memory);
                }
                device = other.device;
                memory = other.memory;
                mappedData = other.mappedData;
                other.mappedData = nullptr;
            }
            return *this;
        }

        void* data() const { return mappedData; }

    private:
        vk::Device device;
        vk::DeviceMemory memory;
        void* mappedData = nullptr;
    };
}
