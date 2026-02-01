#include "TerrainMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "print/EditorLogger.hpp"
#include <cstring>

namespace render::gpudriven
{
    TerrainMeshBuffer::TerrainMeshBuffer(core::Device& device)
        : device_(device)
    {
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager_ = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    TerrainMeshBuffer::~TerrainMeshBuffer()
    {
        cleanup();
    }

    void TerrainMeshBuffer::init(uint32_t maxVertices, uint32_t maxIndices,
                                  uint32_t maxMeshlets, uint32_t maxMeshletVertices,
                                  uint32_t maxMeshletPrimitives)
    {
        if (initialized_)
        {
            vfLogWarning("TerrainMeshBuffer already initialized");
            return;
        }

        maxVertexCount_ = maxVertices;
        maxIndexCount_ = maxIndices;
        maxMeshletCount_ = maxMeshlets;
        maxMeshletVertexCount_ = maxMeshletVertices;
        maxMeshletPrimitiveCount_ = maxMeshletPrimitives;

        vertexAllocator_.reset(maxVertexCount_);
        indexAllocator_.reset(maxIndexCount_);
        meshletAllocator_.reset(maxMeshletCount_);
        meshletVertexAllocator_.reset(maxMeshletVertexCount_);
        meshletPrimitiveAllocator_.reset(maxMeshletPrimitiveCount_);

        createBuffers();

        initialized_ = true;

        vfLogInfo("TerrainMeshBuffer initialized: {}M vertices, {}M indices, {}K meshlets, {}M meshlet verts/prims",
                  maxVertexCount_ / 1000000, maxIndexCount_ / 1000000, maxMeshletCount_ / 1000,
                  maxMeshletPrimitiveCount_ / 1000000);
    }

    void TerrainMeshBuffer::cleanup()
    {
        if (!initialized_) return;

        device_.getLogicalDevice().waitIdle();

        tileAllocations_.clear();
        destroyBuffers();

        currentVertexCount_ = 0;
        currentIndexCount_ = 0;
        currentMeshletCount_ = 0;
        currentMeshletVertexCount_ = 0;
        currentMeshletPrimitiveCount_ = 0;

        initialized_ = false;
        vfLogInfo("TerrainMeshBuffer cleaned up");
    }

    void TerrainMeshBuffer::createBuffers()
    {
        vk::Device vkDevice = device_.getLogicalDevice();
        vk::PhysicalDevice physicalDevice = device_.getPhysicalDevice();

        // Vertex buffer
        {
            core::BufferInfoRequest request(vkDevice, physicalDevice);
            request.size = maxVertexCount_ * VERTEX_STRIDE;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                            vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, vertexBuffer_, vertexBufferMemory_);
        }

        // Index buffer
        {
            core::BufferInfoRequest request(vkDevice, physicalDevice);
            request.size = maxIndexCount_ * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                            vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, indexBuffer_, indexBufferMemory_);
        }

        // Meshlet buffer
        {
            core::BufferInfoRequest request(vkDevice, physicalDevice);
            request.size = maxMeshletCount_ * sizeof(GPUMeshlet);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, meshletBuffer_, meshletBufferMemory_);
        }

        // Meshlet vertex buffer
        {
            core::BufferInfoRequest request(vkDevice, physicalDevice);
            request.size = maxMeshletVertexCount_ * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, meshletVertexBuffer_, meshletVertexBufferMemory_);
        }

        // Meshlet primitive buffer
        {
            core::BufferInfoRequest request(vkDevice, physicalDevice);
            request.size = maxMeshletPrimitiveCount_ * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, meshletPrimitiveBuffer_, meshletPrimitiveBufferMemory_);
        }
    }

    void TerrainMeshBuffer::destroyBuffers()
    {
        vk::Device vkDevice = device_.getLogicalDevice();

        if (meshletPrimitiveBuffer_)
        {
            vkDevice.destroyBuffer(meshletPrimitiveBuffer_);
            vkDevice.freeMemory(meshletPrimitiveBufferMemory_);
            meshletPrimitiveBuffer_ = nullptr;
        }

        if (meshletVertexBuffer_)
        {
            vkDevice.destroyBuffer(meshletVertexBuffer_);
            vkDevice.freeMemory(meshletVertexBufferMemory_);
            meshletVertexBuffer_ = nullptr;
        }

        if (meshletBuffer_)
        {
            vkDevice.destroyBuffer(meshletBuffer_);
            vkDevice.freeMemory(meshletBufferMemory_);
            meshletBuffer_ = nullptr;
        }

        if (indexBuffer_)
        {
            vkDevice.destroyBuffer(indexBuffer_);
            vkDevice.freeMemory(indexBufferMemory_);
            indexBuffer_ = nullptr;
        }

        if (vertexBuffer_)
        {
            vkDevice.destroyBuffer(vertexBuffer_);
            vkDevice.freeMemory(vertexBufferMemory_);
            vertexBuffer_ = nullptr;
        }
    }

    TerrainTileGeometry* TerrainMeshBuffer::allocateTile(
        const std::string& tileKey,
        const std::array<uint32_t, LOD_LEVEL_COUNT>& vertexCounts,
        const std::array<uint32_t, LOD_LEVEL_COUNT>& indexCounts,
        const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletCounts,
        const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletVertexCounts,
        const std::array<uint32_t, LOD_LEVEL_COUNT>& meshletPrimitiveCounts,
        const glm::vec3& aabbMin,
        const glm::vec3& aabbMax)
    {
        if (!initialized_)
        {
            vfLogError("TerrainMeshBuffer: Not initialized");
            return nullptr;
        }

        // Check if already allocated
        auto it = tileAllocations_.find(tileKey);
        if (it != tileAllocations_.end())
        {
            return &it->second;
        }

        TerrainTileGeometry tile;
        tile.tileKey = tileKey;
        tile.aabbMin = aabbMin;
        tile.aabbMax = aabbMax;

        // Calculate bounding sphere
        glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
        float radius = glm::length(aabbMax - center);
        tile.boundingSphere = glm::vec4(center, radius);

        // Allocate space for each LOD
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            if (vertexCounts[lod] > 0)
            {
                std::string debugKey = tileKey + " LOD" + std::to_string(lod);
                if (!allocateLODSpace(tile.lods[lod],
                                      vertexCounts[lod], indexCounts[lod],
                                      meshletCounts[lod], meshletVertexCounts[lod],
                                      meshletPrimitiveCounts[lod], debugKey))
                {
                    // Failed - free previously allocated LODs
                    for (uint32_t prevLod = 0; prevLod < lod; ++prevLod)
                    {
                        freeLODSpace(tile.lods[prevLod]);
                    }
                    vfLogError("TerrainMeshBuffer: Failed to allocate tile {}", tileKey);
                    return nullptr;
                }
            }
        }

        auto [insertIt, success] = tileAllocations_.emplace(tileKey, std::move(tile));
        return &insertIt->second;
    }

    bool TerrainMeshBuffer::allocateLODSpace(TerrainLODGeometry& lod,
                                              uint32_t vertexCount, uint32_t indexCount,
                                              uint32_t meshletCount, uint32_t meshletVertexCount,
                                              uint32_t meshletPrimitiveCount,
                                              const std::string& debugKey)
    {
        // Allocate vertex space
        uint32_t vertexOffset = vertexAllocator_.allocate(vertexCount);
        if (vertexOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vfLogError("TerrainMeshBuffer: Failed to allocate {} vertices for {}", vertexCount, debugKey);
            return false;
        }

        // Allocate index space
        uint32_t indexOffset = indexAllocator_.allocate(indexCount);
        if (indexOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vertexAllocator_.free(vertexOffset, vertexCount);
            vfLogError("TerrainMeshBuffer: Failed to allocate {} indices for {}", indexCount, debugKey);
            return false;
        }

        // Allocate meshlet space
        uint32_t meshletOffset = 0;
        uint32_t meshletVertexOffset = 0;
        uint32_t meshletPrimitiveOffset = 0;

        if (meshletCount > 0)
        {
            meshletOffset = meshletAllocator_.allocate(meshletCount);
            if (meshletOffset == FreeListAllocator::ALLOCATION_FAILED)
            {
                vertexAllocator_.free(vertexOffset, vertexCount);
                indexAllocator_.free(indexOffset, indexCount);
                vfLogError("TerrainMeshBuffer: Failed to allocate {} meshlets for {}", meshletCount, debugKey);
                return false;
            }

            meshletVertexOffset = meshletVertexAllocator_.allocate(meshletVertexCount);
            if (meshletVertexOffset == FreeListAllocator::ALLOCATION_FAILED)
            {
                vertexAllocator_.free(vertexOffset, vertexCount);
                indexAllocator_.free(indexOffset, indexCount);
                meshletAllocator_.free(meshletOffset, meshletCount);
                vfLogError("TerrainMeshBuffer: Failed to allocate {} meshlet vertices for {}", meshletVertexCount, debugKey);
                return false;
            }

            meshletPrimitiveOffset = meshletPrimitiveAllocator_.allocate(meshletPrimitiveCount);
            if (meshletPrimitiveOffset == FreeListAllocator::ALLOCATION_FAILED)
            {
                vertexAllocator_.free(vertexOffset, vertexCount);
                indexAllocator_.free(indexOffset, indexCount);
                meshletAllocator_.free(meshletOffset, meshletCount);
                meshletVertexAllocator_.free(meshletVertexOffset, meshletVertexCount);
                vfLogError("TerrainMeshBuffer: Failed to allocate {} meshlet primitives for {}", meshletPrimitiveCount, debugKey);
                return false;
            }
        }

        lod.vertexOffset = vertexOffset;
        lod.vertexCount = vertexCount;
        lod.indexOffset = indexOffset;
        lod.indexCount = indexCount;
        lod.meshletOffset = meshletOffset;
        lod.meshletCount = meshletCount;
        lod.meshletVertexOffset = meshletVertexOffset;
        lod.meshletVertexCount = meshletVertexCount;
        lod.meshletPrimitiveOffset = meshletPrimitiveOffset;
        lod.meshletPrimitiveCount = meshletPrimitiveCount;
        lod.isAllocated = true;

        currentVertexCount_ += vertexCount;
        currentIndexCount_ += indexCount;
        currentMeshletCount_ += meshletCount;
        currentMeshletVertexCount_ += meshletVertexCount;
        currentMeshletPrimitiveCount_ += meshletPrimitiveCount;

        return true;
    }

    void TerrainMeshBuffer::freeLODSpace(TerrainLODGeometry& lod)
    {
        if (!lod.isAllocated) return;

        if (lod.vertexCount > 0)
        {
            vertexAllocator_.free(lod.vertexOffset, lod.vertexCount);
            currentVertexCount_ -= lod.vertexCount;
        }

        if (lod.indexCount > 0)
        {
            indexAllocator_.free(lod.indexOffset, lod.indexCount);
            currentIndexCount_ -= lod.indexCount;
        }

        if (lod.meshletCount > 0)
        {
            meshletAllocator_.free(lod.meshletOffset, lod.meshletCount);
            currentMeshletCount_ -= lod.meshletCount;
        }

        if (lod.meshletVertexCount > 0)
        {
            meshletVertexAllocator_.free(lod.meshletVertexOffset, lod.meshletVertexCount);
            currentMeshletVertexCount_ -= lod.meshletVertexCount;
        }

        if (lod.meshletPrimitiveCount > 0)
        {
            meshletPrimitiveAllocator_.free(lod.meshletPrimitiveOffset, lod.meshletPrimitiveCount);
            currentMeshletPrimitiveCount_ -= lod.meshletPrimitiveCount;
        }

        lod = TerrainLODGeometry{};
    }

    bool TerrainMeshBuffer::uploadLODVertices(const std::string& tileKey, uint32_t lodLevel,
                                               const resource::Vertex* vertices, uint32_t count)
    {
        if (!initialized_ || lodLevel >= LOD_LEVEL_COUNT) return false;

        auto it = tileAllocations_.find(tileKey);
        if (it == tileAllocations_.end()) return false;

        const auto& lod = it->second.lods[lodLevel];
        if (!lod.isAllocated || lod.vertexCount != count)
        {
            vfLogError("TerrainMeshBuffer: Vertex count mismatch for {} LOD{}", tileKey, lodLevel);
            return false;
        }

        uploadVertexDataAt(lod.vertexOffset, vertices, count);
        return true;
    }

    bool TerrainMeshBuffer::uploadLODIndices(const std::string& tileKey, uint32_t lodLevel,
                                              const uint32_t* indices, uint32_t count)
    {
        if (!initialized_ || lodLevel >= LOD_LEVEL_COUNT) return false;

        auto it = tileAllocations_.find(tileKey);
        if (it == tileAllocations_.end()) return false;

        const auto& lod = it->second.lods[lodLevel];
        if (!lod.isAllocated || lod.indexCount != count)
        {
            vfLogError("TerrainMeshBuffer: Index count mismatch for {} LOD{}", tileKey, lodLevel);
            return false;
        }

        uploadIndexDataAt(lod.indexOffset, indices, count);
        return true;
    }

    bool TerrainMeshBuffer::uploadLODMeshlets(const std::string& tileKey, uint32_t lodLevel,
                                               const GPUMeshlet* meshlets, uint32_t count,
                                               const uint32_t* meshletVertices, uint32_t meshletVertexCount,
                                               const uint32_t* meshletPrimitives, uint32_t meshletPrimitiveCount)
    {
        if (!initialized_ || lodLevel >= LOD_LEVEL_COUNT) return false;

        auto it = tileAllocations_.find(tileKey);
        if (it == tileAllocations_.end()) return false;

        const auto& lod = it->second.lods[lodLevel];
        if (!lod.isAllocated ||
            lod.meshletCount != count ||
            lod.meshletVertexCount != meshletVertexCount ||
            lod.meshletPrimitiveCount != meshletPrimitiveCount)
        {
            vfLogError("TerrainMeshBuffer: Meshlet data mismatch for {} LOD{}", tileKey, lodLevel);
            return false;
        }

        uploadMeshletDataAt(lod.meshletOffset, meshlets, count);
        uploadMeshletVerticesAt(lod.meshletVertexOffset, meshletVertices, meshletVertexCount);
        uploadMeshletPrimitivesAt(lod.meshletPrimitiveOffset, meshletPrimitives, meshletPrimitiveCount);

        return true;
    }

    void TerrainMeshBuffer::uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount)
    {
        vk::DeviceSize byteOffset = offset * VERTEX_STRIDE;
        vk::DeviceSize byteSize = vertexCount * VERTEX_STRIDE;
        transferManager_->copyToBufferAsync(vertexBuffer_, data, byteSize, byteOffset);
    }

    void TerrainMeshBuffer::uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount)
    {
        vk::DeviceSize byteOffset = offset * sizeof(uint32_t);
        vk::DeviceSize byteSize = indexCount * sizeof(uint32_t);
        transferManager_->copyToBufferAsync(indexBuffer_, data, byteSize, byteOffset);
    }

    void TerrainMeshBuffer::uploadMeshletDataAt(uint32_t offset, const GPUMeshlet* data, uint32_t count)
    {
        vk::DeviceSize byteOffset = offset * sizeof(GPUMeshlet);
        vk::DeviceSize byteSize = count * sizeof(GPUMeshlet);
        transferManager_->copyToBufferAsync(meshletBuffer_, data, byteSize, byteOffset);
    }

    void TerrainMeshBuffer::uploadMeshletVerticesAt(uint32_t offset, const uint32_t* data, uint32_t count)
    {
        vk::DeviceSize byteOffset = offset * sizeof(uint32_t);
        vk::DeviceSize byteSize = count * sizeof(uint32_t);
        transferManager_->copyToBufferAsync(meshletVertexBuffer_, data, byteSize, byteOffset);
    }

    void TerrainMeshBuffer::uploadMeshletPrimitivesAt(uint32_t offset, const uint32_t* data, uint32_t count)
    {
        vk::DeviceSize byteOffset = offset * sizeof(uint32_t);
        vk::DeviceSize byteSize = count * sizeof(uint32_t);
        transferManager_->copyToBufferAsync(meshletPrimitiveBuffer_, data, byteSize, byteOffset);
    }

    void TerrainMeshBuffer::freeTile(const std::string& tileKey)
    {
        auto it = tileAllocations_.find(tileKey);
        if (it == tileAllocations_.end()) return;

        for (auto& lod : it->second.lods)
        {
            freeLODSpace(lod);
        }

        tileAllocations_.erase(it);
    }

    bool TerrainMeshBuffer::hasTile(const std::string& tileKey) const
    {
        return tileAllocations_.find(tileKey) != tileAllocations_.end();
    }

    const TerrainTileGeometry* TerrainMeshBuffer::getTileGeometry(const std::string& tileKey) const
    {
        auto it = tileAllocations_.find(tileKey);
        return (it != tileAllocations_.end()) ? &it->second : nullptr;
    }

    TerrainTileGeometry* TerrainMeshBuffer::getTileGeometryMutable(const std::string& tileKey)
    {
        auto it = tileAllocations_.find(tileKey);
        return (it != tileAllocations_.end()) ? &it->second : nullptr;
    }

    void TerrainMeshBuffer::flushPendingTransfers()
    {
        if (transferManager_)
        {
            transferManager_->waitAll();
        }
    }

    void TerrainMeshBuffer::clear()
    {
        for (auto& [key, tile] : tileAllocations_)
        {
            for (auto& lod : tile.lods)
            {
                freeLODSpace(lod);
            }
        }
        tileAllocations_.clear();
    }

    size_t TerrainMeshBuffer::getTotalBufferSize() const
    {
        return getVertexBufferSize() +
               getIndexBufferSize() +
               getMeshletBufferSize() +
               maxMeshletVertexCount_ * sizeof(uint32_t) +
               maxMeshletPrimitiveCount_ * sizeof(uint32_t);
    }
}
