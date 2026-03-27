#pragma once

#include "WaterGPUTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <array>

namespace render::water
{
    class WaterMeshBuffer
    {
    private:
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        vk::Queue graphicsQueue;
        vk::CommandPool commandPool;

        // Shared vertex/index buffers containing all LOD meshes
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexMemory;
        vk::Buffer indexBuffer;
        vk::DeviceMemory indexMemory;

        // Per-LOD mesh info (offsets into shared buffers)
        std::array<WaterLODMeshInfo, WATER_LOD_COUNT> lodMeshes;

        // Per-tile instance SSBO (host-visible, persistent mapped)
        vk::Buffer tileSSBO;
        vk::DeviceMemory tileSSBOMemory;
        void* mappedTileData = nullptr;
        uint32_t currentTileCount = 0;

        bool initialized = false;

    public:
        void init(vk::Device device, vk::PhysicalDevice physicalDevice,
                  vk::Queue queue, vk::CommandPool cmdPool, uint32_t subdivisions);
        void cleanup();

        void updateTileData(const std::vector<WaterTileGPUData>& tiles);

        [[nodiscard]] vk::Buffer getVertexBuffer() const { return vertexBuffer; }
        [[nodiscard]] vk::Buffer getIndexBuffer() const { return indexBuffer; }
        [[nodiscard]] vk::Buffer getTileSSBO() const { return tileSSBO; }
        [[nodiscard]] uint32_t getTileCount() const { return currentTileCount; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        [[nodiscard]] const WaterLODMeshInfo& getLODMesh(uint32_t lod) const { return lodMeshes[lod]; }
        [[nodiscard]] uint32_t getIndexCount() const { return lodMeshes[0].indexCount; } // LOD0 for backward compat

    private:
        void createMultiLODMesh();
        void createTileSSBO();

        static void generateQuadMesh(uint32_t subdivisions,
                                      std::vector<WaterVertex>& outVertices,
                                      std::vector<uint32_t>& outIndices);
    };
}
