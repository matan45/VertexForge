#include "WaterMeshBuffer.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/EditorLogger.hpp"
#include <cstring>

namespace render::water
{
    void WaterMeshBuffer::init(vk::Device device, vk::PhysicalDevice physicalDevice,
                               vk::Queue queue, vk::CommandPool cmdPool, uint32_t subdivisions)
    {
        this->device = device;
        this->physicalDevice = physicalDevice;
        this->graphicsQueue = queue;
        this->commandPool = cmdPool;
        this->subdivisions = subdivisions;

        createQuadMesh();
        createTileSSBO();

        initialized = true;
    }

    void WaterMeshBuffer::cleanup()
    {
        if (!device)
            return;

        if (mappedTileData)
        {
            device.unmapMemory(tileSSBOMemory);
            mappedTileData = nullptr;
        }

        core::BufferUtilities::destroyBuffer(device, tileSSBO, tileSSBOMemory);
        core::BufferUtilities::destroyBuffer(device, vertexBuffer, vertexMemory);
        core::BufferUtilities::destroyBuffer(device, indexBuffer, indexMemory);

        initialized = false;
    }

    void WaterMeshBuffer::createQuadMesh()
    {
        uint32_t N = subdivisions;
        uint32_t vertsPerSide = N + 1;

        // Generate vertices for subdivided unit quad (0,0) -> (1,0,1)
        std::vector<WaterVertex> vertices;
        vertices.reserve(vertsPerSide * vertsPerSide);

        for (uint32_t z = 0; z < vertsPerSide; ++z)
        {
            for (uint32_t x = 0; x < vertsPerSide; ++x)
            {
                float u = static_cast<float>(x) / static_cast<float>(N);
                float v = static_cast<float>(z) / static_cast<float>(N);

                WaterVertex vertex;
                vertex.position = glm::vec3(u, 0.0f, v);
                vertex.texCoord = glm::vec2(u, v);
                vertices.push_back(vertex);
            }
        }

        // Generate indices (two triangles per cell)
        std::vector<uint32_t> indices;
        indices.reserve(N * N * 6);

        for (uint32_t z = 0; z < N; ++z)
        {
            for (uint32_t x = 0; x < N; ++x)
            {
                uint32_t topLeft = z * vertsPerSide + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * vertsPerSide + x;
                uint32_t bottomRight = bottomLeft + 1;

                // Triangle 1
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                // Triangle 2
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        vertexCount = static_cast<uint32_t>(vertices.size());
        indexCount = static_cast<uint32_t>(indices.size());

        // Create device-local vertex buffer
        vk::DeviceSize vertexSize = vertices.size() * sizeof(WaterVertex);
        core::BufferInfoRequest vertexInfo(
            device, physicalDevice, vertexSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(vertexInfo, vertexBuffer, vertexMemory);
        core::BufferUtilities::copyToBuffer(device, physicalDevice, graphicsQueue, commandPool,
                                            vertexBuffer, vertices.data(), vertexSize);

        // Create device-local index buffer
        vk::DeviceSize indexSize = indices.size() * sizeof(uint32_t);
        core::BufferInfoRequest indexInfo(
            device, physicalDevice, indexSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(indexInfo, indexBuffer, indexMemory);
        core::BufferUtilities::copyToBuffer(device, physicalDevice, graphicsQueue, commandPool,
                                            indexBuffer, indices.data(), indexSize);
    }

    void WaterMeshBuffer::createTileSSBO()
    {
        vk::DeviceSize ssboSize = MAX_WATER_TILES * sizeof(WaterTileGPUData);

        core::BufferInfoRequest ssboInfo(
            device, physicalDevice, ssboSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(ssboInfo, tileSSBO, tileSSBOMemory);

        // Persistent map
        mappedTileData = device.mapMemory(tileSSBOMemory, 0, ssboSize);
    }

    void WaterMeshBuffer::updateTileData(const std::vector<WaterTileGPUData>& tiles)
    {
        if (!mappedTileData || tiles.empty())
        {
            currentTileCount = 0;
            return;
        }

        uint32_t count = static_cast<uint32_t>(std::min(tiles.size(),
                                                        static_cast<size_t>(MAX_WATER_TILES)));
        std::memcpy(mappedTileData, tiles.data(), count * sizeof(WaterTileGPUData));
        currentTileCount = count;
    }
}
