#include "BillboardBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

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
        pendingCountReset = true;
    }

    void BillboardBufferManager::resetCountBuffer(vk::CommandBuffer cmd)
    {
        if (!pendingCountReset) return;

        cmd.fillBuffer(countBuffer, 0, sizeof(uint32_t), 0);

        vk::MemoryBarrier memBarrier(
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::DependencyFlags{},
            1, &memBarrier, 0, nullptr, 0, nullptr);

        pendingCountReset = false;
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
