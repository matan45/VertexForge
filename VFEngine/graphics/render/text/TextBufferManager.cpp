#include "TextBufferManager.hpp"

namespace render::text
{
    TextBufferManager::TextBufferManager(core::Device& device)
        : QuadBufferManager{device}
    {
    }

    void TextBufferManager::init()
    {
        createCameraUBO();
        createQuadBuffers(QUAD_VERTICES, QUAD_INDICES);
        createInstanceBuffer();
    }

    void TextBufferManager::cleanUp()
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

    void TextBufferManager::createCameraUBO()
    {
        if (externalCameraBuffer) return;

        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(TextCameraUBO);
        core::BufferUtilities::createBuffer(bufferRequest, cameraUBO, cameraUBOMemory);
    }

    void TextBufferManager::updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos) const
    {
        if (externalCameraBuffer) return;

        TextCameraUBO ubo{};
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

    void TextBufferManager::updateInstanceBuffer(const std::vector<TextCharInstance>& instances)
    {
        uploadInstances(instances.data(), static_cast<uint32_t>(instances.size()));
    }
}
