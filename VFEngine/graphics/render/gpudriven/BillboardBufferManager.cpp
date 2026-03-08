#include "BillboardBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

namespace render::gpudriven
{
    void BillboardBufferManager::init(core::Device& device)
    {
        devicePtr = &device;
        createBuffers(MAX_GPU_BILLBOARDS);
        vfLogInfo("BillboardBufferManager: Initialized with capacity {}", capacity);
    }

    void BillboardBufferManager::cleanup()
    {
        destroyBuffers();
        devicePtr = nullptr;
    }

    void BillboardBufferManager::uploadInstances(const std::vector<BillboardInstanceGPU>& instances)
    {
        if (!devicePtr || instances.empty()) return;

        uint32_t count = static_cast<uint32_t>(std::min(instances.size(), static_cast<size_t>(capacity)));
        vk::DeviceSize dataSize = count * sizeof(BillboardInstanceGPU);

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vk::PhysicalDevice physDevice = devicePtr->getPhysicalDevice();
        vk::Queue queue = devicePtr->getGraphicsQueue();
        vk::CommandPool cmdPool = devicePtr->getStagingCommandPool();

        // Upload instance data to device-local buffer
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice, queue, cmdPool,
                                             instanceBuffer, instances.data(), dataSize);

        // Upload count to device-local count buffer
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice, queue, cmdPool,
                                             countBuffer, &count, sizeof(uint32_t));

        currentInstanceCount = count;
    }

    void BillboardBufferManager::clear()
    {
        currentInstanceCount = 0;

        if (!devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vk::PhysicalDevice physDevice = devicePtr->getPhysicalDevice();
        vk::Queue queue = devicePtr->getGraphicsQueue();
        vk::CommandPool cmdPool = devicePtr->getStagingCommandPool();

        // Zero the count buffer
        uint32_t zero = 0;
        core::BufferUtilities::copyToBuffer(vkDevice, physDevice, queue, cmdPool,
                                             countBuffer, &zero, sizeof(uint32_t));
    }

    void BillboardBufferManager::createBuffers(uint32_t maxInstances)
    {
        if (!devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();
        vk::PhysicalDevice physDevice = devicePtr->getPhysicalDevice();

        capacity = maxInstances;

        // Device-local instance buffer
        {
            vk::DeviceSize size = maxInstances * sizeof(BillboardInstanceGPU);
            core::BufferInfoRequest request(vkDevice, physDevice);
            request.size = size;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, instanceBuffer, instanceBufferMemory);
        }

        // Device-local count buffer
        {
            core::BufferInfoRequest request(vkDevice, physDevice);
            request.size = sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, countBuffer, countBufferMemory);
        }
    }

    void BillboardBufferManager::destroyBuffers()
    {
        if (!devicePtr) return;

        vk::Device vkDevice = devicePtr->getLogicalDevice();

        core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, countBuffer, countBufferMemory);

        capacity = 0;
        currentInstanceCount = 0;
    }
}
