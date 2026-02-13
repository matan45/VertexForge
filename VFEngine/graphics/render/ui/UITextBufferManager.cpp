#include "UITextBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"

namespace render::ui
{
    UITextBufferManager::UITextBufferManager(core::Device& device)
        : device{device}
    {
    }

    void UITextBufferManager::init()
    {
        createQuadBuffers();
        createInstanceBuffer();
    }

    void UITextBufferManager::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (quadVertexBuffer)
        {
            dev.destroyBuffer(quadVertexBuffer);
            dev.freeMemory(quadVertexBufferMemory);
            quadVertexBuffer = nullptr;
        }
        if (quadIndexBuffer)
        {
            dev.destroyBuffer(quadIndexBuffer);
            dev.freeMemory(quadIndexBufferMemory);
            quadIndexBuffer = nullptr;
        }
        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
        }

        currentInstanceCount = 0;
    }

    void UITextBufferManager::createQuadBuffers()
    {
        constexpr vk::DeviceSize vertexBufferSize = sizeof(UIVertex) * QUAD_VERTICES.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, quadVertexBuffer, quadVertexBufferMemory);

        constexpr vk::DeviceSize indexBufferSize = sizeof(uint16_t) * QUAD_INDICES.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, quadIndexBuffer, quadIndexBufferMemory);

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

    void UITextBufferManager::createInstanceBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(UITextCharInstance) * maxInstances;
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferRequest, instanceBuffer, instanceBufferMemory);
    }

    void UITextBufferManager::resizeInstanceBuffer(uint32_t requiredCount)
    {
        auto& dev = device.getLogicalDevice();
        dev.waitIdle();

        if (instanceBuffer)
        {
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
        }

        maxInstances = requiredCount * 2;
        createInstanceBuffer();
    }

    void UITextBufferManager::updateInstanceBuffer(const std::vector<UITextCharInstance>& instances)
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

        void* data;
        vk::DeviceSize bufferSize = sizeof(UITextCharInstance) * currentInstanceCount;
        vk::Result result = device.getLogicalDevice().mapMemory(instanceBufferMemory, 0, bufferSize, {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, instances.data(), bufferSize);
            device.getLogicalDevice().unmapMemory(instanceBufferMemory);
        }
    }
}
