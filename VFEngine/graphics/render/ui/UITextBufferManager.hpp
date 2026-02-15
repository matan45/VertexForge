#pragma once

#include "UITextRenderTypes.hpp"
#include "UIRenderTypes.hpp"
#include <vector>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::ui
{
    class UITextBufferManager
    {
    private:
        core::Device& device;
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;

        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        uint32_t maxInstances = 4096;
        uint32_t currentInstanceCount = 0;

    public:
        explicit UITextBufferManager(core::Device& device);
        ~UITextBufferManager() = default;

        void init();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

        void updateInstanceBuffer(const std::vector<UITextCharInstance>& instances);

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
