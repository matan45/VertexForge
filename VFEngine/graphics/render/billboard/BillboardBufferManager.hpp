#pragma once

#include "BillboardTypes.hpp"
#include <array>
#include <vector>

namespace core
{
    class Device;
}

namespace render::billboard
{
    inline constexpr std::array<BillboardVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},  // Bottom-left
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},  // Bottom-right
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},  // Top-right
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},  // Top-left
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    class BillboardBufferManager
    {
    private:
        core::Device& device;
        
        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;
        vk::Buffer quadIndexBuffer;
        vk::DeviceMemory quadIndexBufferMemory;
        
        vk::Buffer instanceBuffer;
        vk::DeviceMemory instanceBufferMemory;
        uint32_t maxInstances = 1024;
        uint32_t currentInstanceCount = 0;
        
        vk::Buffer cameraUBO;
        vk::DeviceMemory cameraUBOMemory;

    public:
        explicit BillboardBufferManager(core::Device& device);
        ~BillboardBufferManager() = default;

        void init();
        void cleanUp();

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void updateInstanceBuffer(const std::vector<BillboardRenderData>& billboards);

        // Accessors for pipeline binding
        vk::Buffer getQuadVertexBuffer() const { return quadVertexBuffer; }
        vk::Buffer getQuadIndexBuffer() const { return quadIndexBuffer; }
        vk::Buffer getInstanceBuffer() const { return instanceBuffer; }
        vk::Buffer getCameraUBO() const { return cameraUBO; }
        uint32_t getCurrentInstanceCount() const { return currentInstanceCount; }

    private:
        void createQuadBuffers();
        void createInstanceBuffer();
        void createCameraUBO();
    };
}
