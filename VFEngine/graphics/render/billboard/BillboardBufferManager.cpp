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
            device.getMemoryManager().free(cameraUBOAllocation);
            cameraUBOAllocation = {};
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
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOAllocation, device.getMemoryManager());
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

        if (cameraUBOAllocation.mappedPtr)
        {
            std::memcpy(cameraUBOAllocation.mappedPtr, &ubo, sizeof(ubo));
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

        vk::DeviceSize bufferSize = sizeof(BillboardInstanceData) * currentInstanceCount;
        if (instanceBufferAllocation.mappedPtr)
        {
            std::memcpy(instanceBufferAllocation.mappedPtr, instanceData.data(), bufferSize);
        }
    }
}
