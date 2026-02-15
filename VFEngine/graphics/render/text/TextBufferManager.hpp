#pragma once

#include "TextTypes.hpp"
#include <array>
#include <vector>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::text
{
    inline constexpr std::array<TextVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    class TextBufferManager
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

        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

    public:
        explicit TextBufferManager(core::Device& device);
        ~TextBufferManager() = default;

        void init();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void updateInstanceBuffer(const std::vector<TextCharInstance>& instances);

        vk::Buffer getQuadVertexBuffer() const { return quadVertexBuffer; }
        vk::Buffer getQuadIndexBuffer() const { return quadIndexBuffer; }
        vk::Buffer getInstanceBuffer() const { return instanceBuffer; }
        vk::Buffer getCameraUBO() const { return cameraUBO; }
        uint32_t getCurrentInstanceCount() const { return currentInstanceCount; }

    private:
        void createQuadBuffers();
        void createInstanceBuffer();
        void createCameraUBO();
        void resizeInstanceBuffer(uint32_t requiredCount);
    };
}
