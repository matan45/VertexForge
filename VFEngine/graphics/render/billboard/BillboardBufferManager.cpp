#include "BillboardBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"

namespace render::billboard
{
    BillboardBufferManager::BillboardBufferManager(core::Device& device)
        : device{device}
    {
    }

    void BillboardBufferManager::init()
    {
        createCameraUBO();
        createQuadBuffers();
        createInstanceBuffer();
    }

    void BillboardBufferManager::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (cameraUBO)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
        }
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

    void BillboardBufferManager::createCameraUBO()
    {
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(BillboardCameraUBO);
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void BillboardBufferManager::createQuadBuffers()
    {
        constexpr vk::DeviceSize vertexBufferSize = sizeof(BillboardVertex) * QUAD_VERTICES.size();
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

    void BillboardBufferManager::createInstanceBuffer()
    {
        vk::DeviceSize bufferSize = sizeof(BillboardInstanceData) * maxInstances;
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.size = bufferSize;
        bufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufferRequest, instanceBuffer, instanceBufferMemory);
    }

    void BillboardBufferManager::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                  const glm::vec3& cameraPos) const
    {
        BillboardCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(cameraUBOMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(cameraUBOMemory);
        }
    }

    void BillboardBufferManager::updateInstanceBuffer(const std::vector<BillboardRenderData>& billboards)
    {
        if (billboards.empty())
        {
            currentInstanceCount = 0;
            return;
        }

        currentInstanceCount = static_cast<uint32_t>(std::min(billboards.size(),
                                                               static_cast<size_t>(maxInstances)));

        std::vector<BillboardInstanceData> instanceData(currentInstanceCount);
        for (uint32_t i = 0; i < currentInstanceCount; ++i)
        {
            const auto& src = billboards[i];
            instanceData[i].worldPosition = src.worldPosition;
            instanceData[i].atlasIndex = static_cast<float>(src.atlasIndex);
            instanceData[i].size = src.size;
            instanceData[i].sizeMode = src.sizeMode;
            instanceData[i].entityId = src.entityId;
            instanceData[i].colorTint = src.colorTint;
        }

        void* data;
        vk::DeviceSize bufferSize = sizeof(BillboardInstanceData) * currentInstanceCount;
        vk::Result result = device.getLogicalDevice().mapMemory(instanceBufferMemory, 0, bufferSize, {}, &data);
        if (result == vk::Result::eSuccess)
        {
            std::memcpy(data, instanceData.data(), bufferSize);
            device.getLogicalDevice().unmapMemory(instanceBufferMemory);
        }
    }
}
