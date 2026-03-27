#include "WaterMeshBuffer.hpp"
#include "../../core/BufferUtilities.hpp"
#include <cstring>

namespace render::water
{
    static constexpr uint32_t LOD_SUBDIVISIONS[WATER_LOD_COUNT] = {
        WATER_LOD0_SUBDIVISIONS,
        WATER_LOD1_SUBDIVISIONS,
        WATER_LOD2_SUBDIVISIONS,
        WATER_LOD3_SUBDIVISIONS
    };

    void WaterMeshBuffer::init(vk::Device device, vk::PhysicalDevice physicalDevice,
                               vk::Queue queue, vk::CommandPool cmdPool)
    {
        this->device = device;
        this->physicalDevice = physicalDevice;
        this->graphicsQueue = queue;
        this->commandPool = cmdPool;

        createMultiLODMesh();
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

    void WaterMeshBuffer::generateQuadMesh(uint32_t N,
                                            std::vector<WaterVertex>& outVertices,
                                            std::vector<uint32_t>& outIndices)
    {
        uint32_t vertsPerSide = N + 1;

        outVertices.reserve(vertsPerSide * vertsPerSide);
        for (uint32_t z = 0; z < vertsPerSide; ++z)
        {
            for (uint32_t x = 0; x < vertsPerSide; ++x)
            {
                float u = static_cast<float>(x) / static_cast<float>(N);
                float v = static_cast<float>(z) / static_cast<float>(N);

                WaterVertex vertex;
                vertex.position = glm::vec3(u, 0.0f, v);
                vertex.texCoord = glm::vec2(u, v);
                outVertices.push_back(vertex);
            }
        }

        outIndices.reserve(N * N * 6);
        for (uint32_t z = 0; z < N; ++z)
        {
            for (uint32_t x = 0; x < N; ++x)
            {
                uint32_t topLeft = z * vertsPerSide + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * vertsPerSide + x;
                uint32_t bottomRight = bottomLeft + 1;

                outIndices.push_back(topLeft);
                outIndices.push_back(bottomLeft);
                outIndices.push_back(topRight);

                outIndices.push_back(topRight);
                outIndices.push_back(bottomLeft);
                outIndices.push_back(bottomRight);
            }
        }
    }

    void WaterMeshBuffer::createMultiLODMesh()
    {
        // Generate all LOD meshes and concatenate into shared buffers
        std::vector<WaterVertex> allVertices;
        std::vector<uint32_t> allIndices;

        for (uint32_t lod = 0; lod < WATER_LOD_COUNT; ++lod)
        {
            std::vector<WaterVertex> lodVertices;
            std::vector<uint32_t> lodIndices;
            generateQuadMesh(LOD_SUBDIVISIONS[lod], lodVertices, lodIndices);

            auto& info = lodMeshes[lod];
            info.vertexOffset = static_cast<uint32_t>(allVertices.size());
            info.indexOffset = static_cast<uint32_t>(allIndices.size());
            info.vertexCount = static_cast<uint32_t>(lodVertices.size());
            info.indexCount = static_cast<uint32_t>(lodIndices.size());
            info.subdivisions = LOD_SUBDIVISIONS[lod];

            allVertices.insert(allVertices.end(), lodVertices.begin(), lodVertices.end());
            allIndices.insert(allIndices.end(), lodIndices.begin(), lodIndices.end());
        }

        // Upload combined vertex buffer
        vk::DeviceSize vertexSize = allVertices.size() * sizeof(WaterVertex);
        core::BufferInfoRequest vertexInfo(
            device, physicalDevice, vertexSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(vertexInfo, vertexBuffer, vertexMemory);
        core::BufferUtilities::copyToBuffer(device, physicalDevice, graphicsQueue, commandPool,
                                            vertexBuffer, allVertices.data(), vertexSize);

        // Upload combined index buffer
        vk::DeviceSize indexSize = allIndices.size() * sizeof(uint32_t);
        core::BufferInfoRequest indexInfo(
            device, physicalDevice, indexSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(indexInfo, indexBuffer, indexMemory);
        core::BufferUtilities::copyToBuffer(device, physicalDevice, graphicsQueue, commandPool,
                                            indexBuffer, allIndices.data(), indexSize);
    }

    void WaterMeshBuffer::createTileSSBO()
    {
        vk::DeviceSize ssboSize = MAX_OCEAN_GPU_INSTANCES * sizeof(WaterTileGPUData);

        core::BufferInfoRequest ssboInfo(
            device, physicalDevice, ssboSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(ssboInfo, tileSSBO, tileSSBOMemory);

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
                                                        static_cast<size_t>(MAX_OCEAN_GPU_INSTANCES)));
        std::memcpy(mappedTileData, tiles.data(), count * sizeof(WaterTileGPUData));
        currentTileCount = count;
    }
}
