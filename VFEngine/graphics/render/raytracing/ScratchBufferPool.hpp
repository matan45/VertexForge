#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Device.hpp"
#include <vulkan/vulkan.hpp>

namespace render::raytracing
{
    class ScratchBufferPool
    {
    public:
        vk::DeviceAddress acquire(vk::DeviceSize size, core::Device& device)
        {
            if (size <= currentSize && buffer)
            {
                return device.getLogicalDevice().getBufferAddress({buffer});
            }

            // Need to grow
            vk::Device vkDevice = device.getLogicalDevice();
            if (buffer)
            {
                core::BufferUtilities::destroyBuffer(vkDevice, buffer, allocation, device.getMemoryManager());
                buffer = nullptr;
            }

            currentSize = size;
            if (size > peakSize) peakSize = size;

            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = size;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, buffer, allocation, device.getMemoryManager());

            return vkDevice.getBufferAddress({buffer});
        }

        void cleanup(core::Device& device)
        {
            if (buffer)
            {
                core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), buffer, allocation, device.getMemoryManager());
                buffer = nullptr;
            }
            currentSize = 0;
        }

        vk::DeviceSize getPeakSize() const { return peakSize; }

    private:
        vk::Buffer buffer;
        core::VulkanAllocation allocation;
        vk::DeviceSize currentSize = 0;
        vk::DeviceSize peakSize = 0;
    };
}
