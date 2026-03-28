#pragma once

#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <cstring>

namespace render::common
{
    template<typename VertexT, typename InstanceT>
    class QuadBufferManager
    {
    protected:
        core::Device& device;
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        vk::Buffer quadVertexBuffer;
        core::VulkanAllocation quadVertexBufferAllocation;
        vk::Buffer quadIndexBuffer;
        core::VulkanAllocation quadIndexBufferAllocation;

        vk::Buffer instanceBuffer;
        core::VulkanAllocation instanceBufferAllocation;
        uint32_t maxInstances;
        uint32_t currentInstanceCount = 0;

    public:
        explicit QuadBufferManager(core::Device& device, uint32_t initialMaxInstances = 4096)
            : device{device}, maxInstances{initialMaxInstances}
        {
        }

        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

        vk::Buffer getQuadVertexBuffer() const { return quadVertexBuffer; }
        vk::Buffer getQuadIndexBuffer() const { return quadIndexBuffer; }
        vk::Buffer getInstanceBuffer() const { return instanceBuffer; }
        uint32_t getCurrentInstanceCount() const { return currentInstanceCount; }

    protected:
        template<size_t VertCount, size_t IdxCount>
        void createQuadBuffers(const std::array<VertexT, VertCount>& vertices,
                               const std::array<uint16_t, IdxCount>& indices)
        {
            constexpr vk::DeviceSize vertexBufferSize = sizeof(VertexT) * VertCount;
            core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexRequest.size = vertexBufferSize;
            vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

            constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * IdxCount;
            core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexRequest.size = indexBufferSize;
            indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                device.getGraphicsQueue(), device.getStagingCommandPool(),
                quadVertexBuffer, vertices.data(), vertexBufferSize);

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(), device.getPhysicalDevice(),
                device.getGraphicsQueue(), device.getStagingCommandPool(),
                quadIndexBuffer, indices.data(), indexBufferSize);
        }

        void createInstanceBuffer()
        {
            vk::DeviceSize bufferSize = sizeof(InstanceT) * maxInstances;
            core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            bufferRequest.size = bufferSize;
            bufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
            bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                       vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(bufferRequest, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
        }

        void resizeInstanceBuffer(uint32_t requiredCount)
        {
            if (instanceBuffer)
            {
                if (deletionQueue)
                {
                    deletionQueue->queueBuffer(instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
                }
                else
                {
                    auto& dev = device.getLogicalDevice();
                    dev.waitIdle();
                    dev.destroyBuffer(instanceBuffer);
                    device.getMemoryManager().free(instanceBufferAllocation);
                    instanceBufferAllocation = {};
                }
                instanceBuffer = nullptr;
            }

            maxInstances = requiredCount * 2;
            createInstanceBuffer();
        }

        void uploadInstances(const InstanceT* data, uint32_t count)
        {
            if (count == 0)
            {
                currentInstanceCount = 0;
                return;
            }

            if (count > maxInstances)
            {
                resizeInstanceBuffer(count);
            }

            currentInstanceCount = count;

            vk::DeviceSize bufferSize = sizeof(InstanceT) * currentInstanceCount;
            if (instanceBufferAllocation.mappedPtr)
            {
                std::memcpy(instanceBufferAllocation.mappedPtr, data, bufferSize);
            }
        }

        void cleanUpQuadAndInstanceBuffers()
        {
            auto& dev = device.getLogicalDevice();

            if (quadVertexBuffer)
            {
                dev.destroyBuffer(quadVertexBuffer);
                device.getMemoryManager().free(quadVertexBufferAllocation);
                quadVertexBufferAllocation = {};
                quadVertexBuffer = nullptr;
            }
            if (quadIndexBuffer)
            {
                dev.destroyBuffer(quadIndexBuffer);
                device.getMemoryManager().free(quadIndexBufferAllocation);
                quadIndexBufferAllocation = {};
                quadIndexBuffer = nullptr;
            }
            if (instanceBuffer)
            {
                dev.destroyBuffer(instanceBuffer);
                device.getMemoryManager().free(instanceBufferAllocation);
                instanceBufferAllocation = {};
                instanceBuffer = nullptr;
            }

            currentInstanceCount = 0;
        }
    };
}
