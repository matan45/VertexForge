#pragma once

#include "WaterGPUTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace render::water
{
    class WaterMeshBuffer
    {
    private:
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        vk::Queue graphicsQueue;
        vk::CommandPool commandPool;

        // Shared unit quad mesh (device-local, static)
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexMemory;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;

        // Per-tile instance SSBO (host-visible, persistent mapped)
        vk::Buffer tileSSBO;
        vk::DeviceMemory tileSSBOMemory;
        void* mappedTileData = nullptr;
        uint32_t currentTileCount = 0;

        uint32_t subdivisions = WATER_DEFAULT_SUBDIVISIONS;
        bool initialized = false;

    public:
        void init(vk::Device device, vk::PhysicalDevice physicalDevice,
                  vk::Queue queue, vk::CommandPool cmdPool, uint32_t subdivisions);
        void cleanup();

        void updateTileData(const std::vector<WaterTileGPUData>& tiles);

        [[nodiscard]] vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        [[nodiscard]] vk::Buffer getIndexBuffer() const { return indexBuffer; }
        [[nodiscard]] vk::Buffer getTileSSBO() const { return tileSSBO; }
        [[nodiscard]] uint32_t getIndexCount() const { return indexCount; }
        [[nodiscard]] uint32_t getTileCount() const { return currentTileCount; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createQuadMesh();
        void createTileSSBO();
    };
}
