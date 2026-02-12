#pragma once

#include "UIRenderTypes.hpp"
#include <vector>

namespace core
{
    class Device;
}

namespace render::ui
{
    class UIRenderBufferManager
    {
    private:
        core::Device& device;

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;

        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        uint32_t maxInstances = 4096;
        uint32_t currentInstanceCount = 0;

    public:
        explicit UIRenderBufferManager(core::Device& device);
        ~UIRenderBufferManager() = default;

        void init();
        void cleanUp();

        void updateInstanceBuffer(const std::vector<UIImageInstance>& instances);

        vk::Buffer getQuadVertexBuffer() const { return quadVertexBuffer; }
        vk::Buffer getQuadIndexBuffer() const { return quadIndexBuffer; }
        vk::Buffer getInstanceBuffer() const { return instanceBuffer; }
        uint32_t getCurrentInstanceCount() const { return currentInstanceCount; }

    private:
        void createQuadBuffers();
        void createInstanceBuffer();
        void resizeInstanceBuffer(uint32_t requiredCount);
    };
}
