#include "BillboardBufferManager.hpp"

namespace render::billboard
{
    BillboardBufferManager::BillboardBufferManager(core::Device& device)
        : QuadBufferManager{device, 1024}
    {
    }

    void BillboardBufferManager::init()
    {
        createCameraUBO();
        createQuadBuffers(QUAD_VERTICES, QUAD_INDICES);
        createInstanceBuffer();
    }

    void BillboardBufferManager::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (cameraUBO && !externalCameraBuffer)
        {
            dev.destroyBuffer(cameraUBO);
            dev.freeMemory(cameraUBOMemory);
            cameraUBO = nullptr;
        }

        cleanUpQuadAndInstanceBuffers();
    }

    void BillboardBufferManager::createCameraUBO()
    {
        if (externalCameraBuffer) return;

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(BillboardCameraUBO);
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void BillboardBufferManager::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                  const glm::vec3& cameraPos) const
    {
        if (externalCameraBuffer) return;

        BillboardCameraUBO ubo{};
        ubo.view = view;
        ubo.projection = projection;
        ubo.cameraPos = cameraPos;
        ubo.time = 0.0f;

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

        uint32_t count = static_cast<uint32_t>(billboards.size());

        if (count > maxInstances)
        {
            resizeInstanceBuffer(count);
        }

        currentInstanceCount = count;

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
