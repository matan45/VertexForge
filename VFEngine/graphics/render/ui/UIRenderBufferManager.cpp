#include "UIRenderBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/DeferredDeletionQueue.hpp"

namespace render::ui
{
    UIRenderBufferManager::UIRenderBufferManager(core::Device& device)
        : device{device}
    {
    }

    void UIRenderBufferManager::init()
    {
        createQuadBuffers();
        createInstanceBuffer();
    }

    void UIRenderBufferManager::cleanUp()
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

    void UIRenderBufferManager::createQuadBuffers()
    {
        constexpr vk::DeviceSize vertexBufferSize = sizeof(UIVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferAllocation, device.getMemoryManager());

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadVertexBuffer,
            QUAD_VERTICES.data(),
            vertexBufferSize
        );

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            quadIndexBuffer,
            QUAD_INDICES.data(),
            indexBufferSize
        );
    }

    void UIRenderBufferManager::createInstanceBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(UIImageInstance) * maxInstances;
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferRequest, instanceBuffer, instanceBufferAllocation, device.getMemoryManager());
    }

    void UIRenderBufferManager::resizeInstanceBuffer(uint32_t requiredCount)
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

    void UIRenderBufferManager::updateInstanceBuffer(const std::vector<UIImageInstance>& instances)
    {
        if (instances.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        uint32_t count = static_cast<uint32_t>(instances.size());

        if (count > maxInstances)
        {
            resizeInstanceBuffer(count);
        }

        currentInstanceCount = count;

        vk::DeviceSize bufferSize = sizeof(UIImageInstance) * currentInstanceCount;
        if (instanceBufferAllocation.mappedPtr)
        {
            std::memcpy(instanceBufferAllocation.mappedPtr, instances.data(), bufferSize);
        }
    }
}
