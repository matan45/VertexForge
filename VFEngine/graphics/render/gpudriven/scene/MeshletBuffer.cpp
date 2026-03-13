#include "print/Log.hpp"
#include "MeshletBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/TransferManager.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/MeshStreamHandle.hpp"
#include <cassert>

namespace render::gpudriven
{
    MeshletBuffer::MeshletBuffer(core::Device& device)
        : device(device)
    {
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    MeshletBuffer::~MeshletBuffer()
    {
        cleanup();
    }

    void MeshletBuffer::init(uint32_t maxMeshlets, uint32_t maxVertexIndices, uint32_t maxPrimitives)
    {
        if (initialized)
        {
            vfLogWarning("MeshletBuffer: Already initialized");
            return;
        }

        maxMeshletCount = maxMeshlets;
        maxVertexIndexCount = maxVertexIndices;
        maxPrimitiveCount = maxPrimitives;

        meshletAllocator.reset(maxMeshletCount);
        vertexIndexAllocator.reset(maxVertexIndexCount);
        primitiveAllocator.reset(maxPrimitiveCount);

        createBuffers();

        initialized = true;

        vfLogInfo("MeshletBuffer: Initialized with {} meshlets, {} vertex indices, {} primitives",
                  maxMeshletCount, maxVertexIndexCount, maxPrimitiveCount);
        vfLogInfo("MeshletBuffer: Total buffer size: {} MB",
                  getTotalBufferSize() / (1024 * 1024));
    }

    void MeshletBuffer::cleanup()
    {
        if (!initialized) return;

        flushPendingTransfers();

        allocations.clear();
        allocationKeyToIndex.clear();
        freeAllocationSlots.clear();

        destroyBuffers();

        currentMeshletCount = 0;
        currentVertexIndexCount = 0;
        currentPrimitiveCount = 0;

        initialized = false;
    }

    void MeshletBuffer::createBuffers()
    {
        auto logicalDevice = device.getLogicalDevice();

        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getMeshletBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            meshletBuffer,
            meshletBufferMemory
        );

        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getMeshletVertexBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            meshletVertexBuffer,
            meshletVertexBufferMemory
        );

        core::BufferUtilities::createBuffer(
            core::BufferInfoRequest{
                logicalDevice, device.getPhysicalDevice(),
                getMeshletPrimitiveBufferSize(),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            },
            meshletPrimitiveBuffer,
            meshletPrimitiveBufferMemory
        );
    }

    void MeshletBuffer::destroyBuffers()
    {
        auto logicalDevice = device.getLogicalDevice();

        if (meshletBuffer)
        {
            logicalDevice.destroyBuffer(meshletBuffer);
            meshletBuffer = nullptr;
        }
        if (meshletBufferMemory)
        {
            logicalDevice.freeMemory(meshletBufferMemory);
            meshletBufferMemory = nullptr;
        }

        if (meshletVertexBuffer)
        {
            logicalDevice.destroyBuffer(meshletVertexBuffer);
            meshletVertexBuffer = nullptr;
        }
        if (meshletVertexBufferMemory)
        {
            logicalDevice.freeMemory(meshletVertexBufferMemory);
            meshletVertexBufferMemory = nullptr;
        }

        if (meshletPrimitiveBuffer)
        {
            logicalDevice.destroyBuffer(meshletPrimitiveBuffer);
            meshletPrimitiveBuffer = nullptr;
        }
        if (meshletPrimitiveBufferMemory)
        {
            logicalDevice.freeMemory(meshletPrimitiveBufferMemory);
            meshletPrimitiveBufferMemory = nullptr;
        }
    }

    MeshletAllocation* MeshletBuffer::reserveMeshlets(const std::string& meshPath,
                                                      const resource::MeshStreamHeader& header)
    {
        if (!initialized)
        {
            vfLogError("MeshletBuffer: Not initialized");
            return nullptr;
        }

        std::vector<std::string> allocatedKeys;

        for (uint32_t submeshIdx = 0; submeshIdx < header.numSubmeshes; ++submeshIdx)
        {
            const auto& submeshInfo = header.submeshes[submeshIdx];

            if (!submeshInfo.hasMeshletData)
            {
                continue;
            }

            std::string key = makeAllocationKey(meshPath, submeshInfo.name, submeshIdx);

            if (allocationKeyToIndex.find(key) != allocationKeyToIndex.end())
            {
                continue;
            }

            MeshletAllocation alloc;
            alloc.meshPath = meshPath;
            alloc.submeshName = submeshInfo.name;
            alloc.submeshIndex = submeshIdx;

            bool allocationFailed = false;
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const auto& meshletInfo = submeshInfo.meshletLods[lod];

                if (meshletInfo.meshletCount > 0)
                {
                    if (!allocateLODMeshletSpace(alloc.lods[lod],
                                                 meshletInfo.meshletCount,
                                                 meshletInfo.vertexIndexCount,
                                                 meshletInfo.primitiveCount,
                                                 key + " LOD" + std::to_string(lod)))
                    {
                        for (uint32_t prevLod = 0; prevLod < lod; ++prevLod)
                        {
                            freeLODMeshletSpace(alloc.lods[prevLod]);
                        }
                        vfLogError("MeshletBuffer: Failed to reserve space for {} LOD{}",
                                   key, lod);
                        allocationFailed = true;
                        break;
                    }
                }
            }

            if (allocationFailed)
            {
                for (const auto& prevKey : allocatedKeys)
                {
                    auto it = allocationKeyToIndex.find(prevKey);
                    if (it != allocationKeyToIndex.end())
                    {
                        size_t allocIndex = it->second;
                        auto& prevAlloc = allocations[allocIndex];
                        for (auto& lodAlloc : prevAlloc.lods)
                        {
                            freeLODMeshletSpace(lodAlloc);
                        }
                        prevAlloc = MeshletAllocation{};
                        freeAllocationSlots.push_back(allocIndex);
                        allocationKeyToIndex.erase(it);
                    }
                }
                return nullptr;
            }

            size_t allocIndex;
            if (!freeAllocationSlots.empty())
            {
                allocIndex = freeAllocationSlots.back();
                freeAllocationSlots.pop_back();
                allocations[allocIndex] = std::move(alloc);
            }
            else
            {
                allocIndex = allocations.size();
                allocations.push_back(std::move(alloc));
            }
            allocationKeyToIndex[key] = allocIndex;
            allocatedKeys.push_back(key);
        }

        for (uint32_t submeshIdx = 0; submeshIdx < header.numSubmeshes; ++submeshIdx)
        {
            const auto& submeshInfo = header.submeshes[submeshIdx];
            std::string key = makeAllocationKey(meshPath, submeshInfo.name, submeshIdx);
            auto it = allocationKeyToIndex.find(key);
            if (it != allocationKeyToIndex.end())
            {
                return &allocations[it->second];
            }
        }

        return nullptr;
    }

    bool MeshletBuffer::allocateLODMeshletSpace(LODAllocation& lodAlloc,
                                                uint32_t meshletCount,
                                                uint32_t vertexIndexCount,
                                                uint32_t primitiveCount,
                                                const std::string& debugKey)
    {
        if (meshletCount == 0)
        {
            lodAlloc.isAllocated = false;
            return true;
        }

        uint32_t meshletOffset = meshletAllocator.allocate(meshletCount);
        if (meshletOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vfLogError("MeshletBuffer: Failed to allocate {} meshlets for {}",
                       meshletCount, debugKey);
            return false;
        }

        uint32_t vertexOffset = vertexIndexAllocator.allocate(vertexIndexCount);
        if (vertexOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            meshletAllocator.free(meshletOffset, meshletCount);
            vfLogError("MeshletBuffer: Failed to allocate {} vertex indices for {}",
                       vertexIndexCount, debugKey);
            return false;
        }

        uint32_t primitiveOffset = primitiveAllocator.allocate(primitiveCount);
        if (primitiveOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            meshletAllocator.free(meshletOffset, meshletCount);
            vertexIndexAllocator.free(vertexOffset, vertexIndexCount);
            vfLogError("MeshletBuffer: Failed to allocate {} primitives for {}",
                       primitiveCount, debugKey);
            return false;
        }

        lodAlloc.meshletOffset = meshletOffset;
        lodAlloc.meshletCount = meshletCount;
        lodAlloc.vertexOffset = vertexOffset;
        lodAlloc.vertexCount = vertexIndexCount;
        lodAlloc.primitiveOffset = primitiveOffset;
        lodAlloc.primitiveCount = primitiveCount;
        lodAlloc.isAllocated = true;

        return true;
    }

    void MeshletBuffer::freeLODMeshletSpace(LODAllocation& lodAlloc)
    {
        if (!lodAlloc.isAllocated) return;

        if (lodAlloc.meshletCount > 0)
        {
            meshletAllocator.free(lodAlloc.meshletOffset, lodAlloc.meshletCount);
        }
        if (lodAlloc.vertexCount > 0)
        {
            vertexIndexAllocator.free(lodAlloc.vertexOffset, lodAlloc.vertexCount);
        }
        if (lodAlloc.primitiveCount > 0)
        {
            primitiveAllocator.free(lodAlloc.primitiveOffset, lodAlloc.primitiveCount);
        }

        lodAlloc = {};
    }

    bool MeshletBuffer::uploadMeshletData(const std::string& meshPath,
                                          const std::string& submeshName,
                                          uint32_t submeshIndex,
                                          uint32_t lodLevel,
                                          const resource::SubmeshMeshletData& meshletData,
                                          uint32_t baseVertexOffset)
    {
        if (!initialized)
        {
            vfLogError("MeshletBuffer: Not initialized");
            return false;
        }

        if (lodLevel >= resource::LOD_LEVEL_COUNT)
        {
            vfLogError("MeshletBuffer: Invalid LOD level {}", lodLevel);
            return false;
        }

        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it == allocationKeyToIndex.end())
        {
            vfLogError("MeshletBuffer: No allocation found for {}", key);
            return false;
        }

        auto& alloc = allocations[it->second];
        auto& lodAlloc = alloc.lods[lodLevel];

        if (!lodAlloc.isAllocated)
        {
            vfLogError("MeshletBuffer: LOD {} not allocated for {}", lodLevel, key);
            return false;
        }

        const auto& lodInfo = meshletData.lodLevels[lodLevel];

        std::vector<GPUMeshlet> gpuMeshlets(lodAlloc.meshletCount);

        uint32_t localMeshletStart = lodInfo.meshletOffset;
        for (uint32_t i = 0; i < lodAlloc.meshletCount; ++i)
        {
            const auto& srcMeshlet = meshletData.meshlets[localMeshletStart + i];
            auto& dstMeshlet = gpuMeshlets[i];

            dstMeshlet.vertexOffset = lodAlloc.vertexOffset + srcMeshlet.descriptor.vertexOffset;
            dstMeshlet.primitiveOffset = lodAlloc.primitiveOffset + srcMeshlet.descriptor.primitiveOffset;
            dstMeshlet.vertexCount = srcMeshlet.descriptor.vertexCount;
            dstMeshlet.primitiveCount = srcMeshlet.descriptor.primitiveCount;
            dstMeshlet.padding0 = 0;
            dstMeshlet.globalVertexOffset = baseVertexOffset;
            dstMeshlet.boundingSphere = srcMeshlet.bounds.boundingSphere;
            dstMeshlet.cone = srcMeshlet.bounds.cone;
        }

        uploadMeshletDataAt(lodAlloc.meshletOffset, gpuMeshlets.data(), lodAlloc.meshletCount);

        std::vector<uint32_t> lodVertexIndices(lodAlloc.vertexCount);
        for (uint32_t i = 0; i < lodAlloc.vertexCount; ++i)
        {
            lodVertexIndices[i] = meshletData.meshletVertices[lodInfo.vertexDataOffset + i];
        }
        uploadVertexIndicesAt(lodAlloc.vertexOffset, lodVertexIndices.data(), lodAlloc.vertexCount);

        std::vector<uint32_t> lodPrimitives(lodAlloc.primitiveCount);
        for (uint32_t i = 0; i < lodAlloc.primitiveCount; ++i)
        {
            lodPrimitives[i] = meshletData.meshletPrimitives[lodInfo.primitiveDataOffset + i];
        }
        uploadPrimitivesAt(lodAlloc.primitiveOffset, lodPrimitives.data(), lodAlloc.primitiveCount);

        meshletAllocator.markUsed(lodAlloc.meshletCount);
        vertexIndexAllocator.markUsed(lodAlloc.vertexCount);
        primitiveAllocator.markUsed(lodAlloc.primitiveCount);

        currentMeshletCount = meshletAllocator.getUsedCount();
        currentVertexIndexCount = vertexIndexAllocator.getUsedCount();
        currentPrimitiveCount = primitiveAllocator.getUsedCount();

        return true;
    }

    void MeshletBuffer::uploadMeshletDataAt(uint32_t offset, const GPUMeshlet* data, uint32_t count)
    {
        if (!data || count == 0) return;

        assert(offset + count <= maxMeshletCount && "Meshlet upload exceeds buffer bounds");

        size_t dataSize = count * sizeof(GPUMeshlet);
        size_t dstOffset = offset * sizeof(GPUMeshlet);

        transferManager->copyToBufferAsync(meshletBuffer, data, dataSize, dstOffset);
    }

    void MeshletBuffer::uploadVertexIndicesAt(uint32_t offset, const uint32_t* data, uint32_t count)
    {
        if (!data || count == 0) return;

        assert(offset + count <= maxVertexIndexCount && "Vertex index upload exceeds buffer bounds");

        size_t dataSize = count * sizeof(uint32_t);
        size_t dstOffset = offset * sizeof(uint32_t);

        transferManager->copyToBufferAsync(meshletVertexBuffer, data, dataSize, dstOffset);
    }

    void MeshletBuffer::uploadPrimitivesAt(uint32_t offset, const uint32_t* data, uint32_t count)
    {
        if (!data || count == 0) return;

        assert(offset + count <= maxPrimitiveCount && "Primitive upload exceeds buffer bounds");

        size_t dataSize = count * sizeof(uint32_t);
        size_t dstOffset = offset * sizeof(uint32_t);

        transferManager->copyToBufferAsync(meshletPrimitiveBuffer, data, dataSize, dstOffset);
    }

    const MeshletAllocation* MeshletBuffer::getAllocation(const std::string& meshPath,
                                                          const std::string& submeshName,
                                                          uint32_t submeshIndex) const
    {
        std::string key = makeAllocationKey(meshPath, submeshName, submeshIndex);
        auto it = allocationKeyToIndex.find(key);
        if (it != allocationKeyToIndex.end())
        {
            return &allocations[it->second];
        }
        return nullptr;
    }

    MeshletLODInfo MeshletBuffer::getMeshletLODInfo(const MeshletAllocation& alloc, uint32_t lodLevel) const
    {
        MeshletLODInfo info{};
        if (lodLevel >= resource::LOD_LEVEL_COUNT) return info;

        const auto& lodAlloc = alloc.lods[lodLevel];
        if (lodAlloc.isAllocated)
        {
            info.meshletOffset = lodAlloc.meshletOffset;
            info.meshletCount = lodAlloc.meshletCount;
            info.baseVertexOffset = 0; // Stored in GPUMeshlet.globalVertexOffset instead
            info.padding = 0;
        }

        return info;
    }

    void MeshletBuffer::freeAllMeshlets(const std::string& meshPath)
    {
        if (!initialized) return;

        std::vector<std::string> keysToRemove;
        for (const auto& [key, allocIdx] : allocationKeyToIndex)
        {
            auto& alloc = allocations[allocIdx];
            if (alloc.meshPath == meshPath)
            {
                for (auto& lodAlloc : alloc.lods)
                {
                    freeLODMeshletSpace(lodAlloc);
                }
                alloc = MeshletAllocation{};
                freeAllocationSlots.push_back(allocIdx);
                keysToRemove.push_back(key);
            }
        }

        for (const auto& key : keysToRemove)
        {
            allocationKeyToIndex.erase(key);
        }

        currentMeshletCount = meshletAllocator.getUsedCount();
        currentVertexIndexCount = vertexIndexAllocator.getUsedCount();
        currentPrimitiveCount = primitiveAllocator.getUsedCount();

        if (!keysToRemove.empty())
        {
            vfLogInfo("MeshletBuffer::freeAllMeshlets: Freed {} allocations for '{}'",
                      keysToRemove.size(), meshPath);
        }
    }

    void MeshletBuffer::flushPendingTransfers()
    {
        if (transferManager && transferManager->hasPendingTransfers())
        {
            transferManager->waitAll();
        }
    }

}
