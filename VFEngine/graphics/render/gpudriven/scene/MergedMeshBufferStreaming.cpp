#include "MergedMeshBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace render::gpudriven
{
    namespace
    {
        void computeSubmeshBounds(SubmeshLocation& loc, const resource::Vertex* vertexData, uint32_t vertexCount)
        {
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

            for (uint32_t i = 0; i < vertexCount; ++i)
            {
                const auto& v = vertexData[i];
                minBounds = glm::min(minBounds, v.position);
                maxBounds = glm::max(maxBounds, v.position);
            }

            loc.aabbMin = minBounds;
            loc.aabbMax = maxBounds;
            loc.calculateBoundingSphere();
        }
    }

    void MergedMeshBuffer::uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount)
    {
        if (!data || vertexCount == 0) return;
        transferManager->copyToBufferAsync(vertexBuffer, data, vertexCount * vertexStride, offset * vertexStride);
    }

    void MergedMeshBuffer::uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount)
    {
        if (!data || indexCount == 0) return;
        transferManager->copyToBufferAsync(indexBuffer, data, indexCount * sizeof(uint32_t), offset * sizeof(uint32_t));
    }

    bool MergedMeshBuffer::allocateLODSpace(SubmeshLocation& loc, uint32_t lodLevel,
                                            uint32_t vertexCount, uint32_t indexCount,
                                            const std::string& meshPath)
    {
        if (vertexCount == 0)
        {
            if (lodLevel > 0)
            {
                loc.lods[lodLevel] = loc.lods[lodLevel - 1];
                loc.lodStates[lodLevel] = loc.lodStates[lodLevel - 1];
            }
            else
            {
                loc.lods[lodLevel] = {0, 0, 0, 0};
                loc.lodStates[lodLevel] = LODStreamState::NotRequested;
            }
            return true;
        }

        uint32_t vertOffset = vertexAllocator.allocate(vertexCount);
        if (vertOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vfLogError("MergedMeshBuffer: Failed to allocate {} vertices for {} LOD{}",
                        vertexCount, meshPath, lodLevel);
            return false;
        }

        uint32_t idxOffset = indexAllocator.allocate(indexCount);
        if (idxOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vertexAllocator.free(vertOffset, vertexCount);
            vfLogError("MergedMeshBuffer: Failed to allocate {} indices for {} LOD{}",
                        indexCount, meshPath, lodLevel);
            return false;
        }

        loc.lods[lodLevel].vertexOffset = vertOffset;
        loc.lods[lodLevel].indexOffset = idxOffset;
        loc.lods[lodLevel].vertexCount = vertexCount;
        loc.lods[lodLevel].indexCount = indexCount;
        loc.lodStates[lodLevel] = LODStreamState::NotRequested;

        return true;
    }

    MergedMeshInfo* MergedMeshBuffer::reserveMesh(const std::string& meshPath,
                                                  const resource::MeshStreamHeader& header)
    {
        if (!initialized)
        {
            vfLogError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end())
            return &registeredMeshes[it->second];

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& submeshStreamInfo = header.submeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = submeshStreamInfo.name;
            loc.submeshIndex = subIdx;
            loc.aabbMin = glm::vec3(0.0f);
            loc.aabbMax = glm::vec3(0.0f);
            loc.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

            for (uint32_t lodIdx = 0; lodIdx < LOD_LEVEL_COUNT; ++lodIdx)
            {
                const auto& lodFileInfo = submeshStreamInfo.lods[lodIdx];
                if (!allocateLODSpace(loc, lodIdx, lodFileInfo.vertexCount, lodFileInfo.indexCount, meshPath))
                    return nullptr;
            }

            std::string key = makeSubmeshKey(meshPath, submeshStreamInfo.name, subIdx);
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        if (!freeMeshSlots.empty())
        {
            size_t slot = freeMeshSlots.back();
            freeMeshSlots.pop_back();
            meshPathToIndex[meshPath] = slot;
            registeredMeshes[slot] = std::move(meshInfo);
            vfLogInfo("MergedMeshBuffer: Reserved space for mesh {} with {} submeshes (reused slot {})",
                       meshPath, header.numSubmeshes, slot);
            return &registeredMeshes[slot];
        }

        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        vfLogInfo("MergedMeshBuffer: Reserved space for mesh {} with {} submeshes",
                   meshPath, header.numSubmeshes);
        return &registeredMeshes.back();
    }

    bool MergedMeshBuffer::uploadLOD(const std::string& meshPath,
                                     const std::string& submeshName,
                                     uint32_t submeshIndex,
                                     uint32_t lodLevel,
                                     const LODUploadData& data)
    {
        if (!initialized || lodLevel >= LOD_LEVEL_COUNT) return false;

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (!loc)
        {
            vfLogError("MergedMeshBuffer::uploadLOD: Submesh not found: {}:{}#{}", meshPath, submeshName, submeshIndex);
            return false;
        }

        const auto& lodInfo = loc->lods[lodLevel];

        if (lodInfo.vertexCount != data.vertexCount || lodInfo.indexCount != data.indexCount)
        {
            vfLogError("MergedMeshBuffer::uploadLOD: Count mismatch for {}:{} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        meshPath, submeshName, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        data.vertexCount, data.indexCount);
            return false;
        }

        if (data.vertexData && data.vertexCount > 0)
            uploadVertexDataAt(lodInfo.vertexOffset, data.vertexData, data.vertexCount);

        if (data.indexData && data.indexCount > 0)
            uploadIndexDataAt(lodInfo.indexOffset, data.indexData, data.indexCount);

        loc->lodStates[lodLevel] = LODStreamState::Uploading;

        bool boundsNotComputed = (loc->aabbMin == glm::vec3(0.0f) && loc->aabbMax == glm::vec3(0.0f));
        if (data.vertexData && data.vertexCount > 0 && boundsNotComputed)
            computeSubmeshBounds(*loc, data.vertexData, data.vertexCount);

        return true;
    }

    void MergedMeshBuffer::markLODReady(const std::string& meshPath,
                                        const std::string& submeshName,
                                        uint32_t submeshIndex,
                                        uint32_t lodLevel)
    {
        if (lodLevel >= LOD_LEVEL_COUNT) return;

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (loc)
        {
            loc->lodStates[lodLevel] = LODStreamState::Ready;

            vertexAllocator.markUsed(loc->lods[lodLevel].vertexCount);
            indexAllocator.markUsed(loc->lods[lodLevel].indexCount);

            totalVertexCount = vertexAllocator.getUsedCount();
            totalIndexCount = indexAllocator.getUsedCount();

            peakVertexCount = std::max(peakVertexCount, totalVertexCount);
            peakIndexCount = std::max(peakIndexCount, totalIndexCount);
        }
    }

    SubmeshLocation* MergedMeshBuffer::getSubmeshLocationMutable(const std::string& meshPath,
                                                                 const std::string& submeshName,
                                                                 uint32_t submeshIndex)
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end())
            return &allSubmeshLocations[it->second];
        return nullptr;
    }

    void MergedMeshBuffer::freeMesh(const std::string& meshPath)
    {
        if (!initialized) return;

        auto pathIt = meshPathToIndex.find(meshPath);
        if (pathIt == meshPathToIndex.end())
        {
            vfLogWarning("MergedMeshBuffer::freeMesh: Mesh '{}' not found", meshPath);
            return;
        }

        size_t meshIdx = pathIt->second;
        auto& meshInfo = registeredMeshes[meshIdx];

        for (uint32_t subIdx = 0; subIdx < meshInfo.submeshCount; ++subIdx)
        {
            std::string key = makeSubmeshKey(meshPath, meshInfo.submeshes[subIdx].submeshName, subIdx);
            auto keyIt = submeshKeyToIndex.find(key);
            if (keyIt == submeshKeyToIndex.end()) continue;

            size_t locIdx = keyIt->second;
            auto& loc = allSubmeshLocations[locIdx];

            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                const auto& lodInfo = loc.lods[lod];
                if (lodInfo.vertexCount > 0) vertexAllocator.free(lodInfo.vertexOffset, lodInfo.vertexCount);
                if (lodInfo.indexCount > 0) indexAllocator.free(lodInfo.indexOffset, lodInfo.indexCount);
                loc.lodStates[lod] = LODStreamState::NotRequested;
            }

            submeshKeyToIndex.erase(keyIt);
        }

        totalVertexCount = vertexAllocator.getUsedCount();
        totalIndexCount = indexAllocator.getUsedCount();

        meshPathToIndex.erase(pathIt);
        meshInfo = MergedMeshInfo{};
        freeMeshSlots.push_back(meshIdx);

        vfLogInfo("MergedMeshBuffer::freeMesh: Freed mesh '{}'", meshPath);
    }

    void MergedMeshBuffer::setPersistentMode(bool enabled)
    {
        if (persistentMode == enabled) return;
        persistentMode = enabled;

        if (enabled)
            objectAllocator.reset(maxObjectCount);
        else
            objectAllocator.reset(0);

        entityToSlot.clear();
        dirtySlots.clear();
        activeObjectCount = 0;
    }

    uint32_t MergedMeshBuffer::allocateObjectSlot()
    {
        uint32_t slot = objectAllocator.allocate(1);
        if (slot != FreeListAllocator::ALLOCATION_FAILED)
            objectAllocator.markUsed(1);
        return slot;
    }

    void MergedMeshBuffer::freeObjectSlot(uint32_t slot)
    {
        if (slot < maxObjectCount)
        {
            objectAllocator.free(slot, 1);
            cpuObjectData[slot] = GPUObjectData{};
        }
    }

    void MergedMeshBuffer::updateObjectAtSlot(uint32_t slot, const GPUObjectData& data)
    {
        if (slot < maxObjectCount)
        {
            cpuObjectData[slot] = data;
            dirtySlots.push_back(slot);
        }
    }

    void MergedMeshBuffer::rebuildActiveIndexList()
    {
        activeObjectCount = 0;
        for (const auto& [uuid, slot] : entityToSlot)
            activeObjectIndices[activeObjectCount++] = slot;
    }

    void MergedMeshBuffer::uploadDirtyObjects(vk::CommandBuffer cmd)
    {
        if (dirtySlots.empty()) return;

        std::sort(dirtySlots.begin(), dirtySlots.end());
        dirtySlots.erase(std::unique(dirtySlots.begin(), dirtySlots.end()), dirtySlots.end());

        peakObjectCount = std::max(peakObjectCount, static_cast<uint32_t>(entityToSlot.size()));

        std::vector<vk::BufferCopy> copyRegions;
        uint32_t rangeStart = dirtySlots[0];
        uint32_t rangeEnd = dirtySlots[0];

        auto& sf = stagingFrames[currentStagingFrame];

        auto emitRange = [&](uint32_t start, uint32_t end) {
            size_t srcOffset = start * sizeof(GPUObjectData);
            size_t rangeSize = (end - start + 1) * sizeof(GPUObjectData);
            std::memcpy(static_cast<uint8_t*>(sf.objectStagingMapped) + srcOffset,
                        cpuObjectData.data() + start, rangeSize);
            copyRegions.push_back({srcOffset, srcOffset, rangeSize});
        };

        for (size_t i = 1; i < dirtySlots.size(); ++i)
        {
            if (dirtySlots[i] <= rangeEnd + 4)
            {
                rangeEnd = dirtySlots[i];
            }
            else
            {
                emitRange(rangeStart, rangeEnd);
                rangeStart = dirtySlots[i];
                rangeEnd = dirtySlots[i];
            }
        }
        emitRange(rangeStart, rangeEnd);

        if (copyRegions.size() > 32)
        {
            uint32_t first = dirtySlots.front();
            uint32_t last = dirtySlots.back();
            size_t srcOffset = first * sizeof(GPUObjectData);
            size_t rangeSize = (last - first + 1) * sizeof(GPUObjectData);
            std::memcpy(static_cast<uint8_t*>(sf.objectStagingMapped) + srcOffset,
                        cpuObjectData.data() + first, rangeSize);
            copyRegions.clear();
            copyRegions.push_back({srcOffset, srcOffset, rangeSize});
        }

        cmd.copyBuffer(sf.objectStagingBuffer, objectBuffer,
                       static_cast<uint32_t>(copyRegions.size()), copyRegions.data());

        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = objectBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, {}, barrier, {});

        dirtySlots.clear();
    }

    void MergedMeshBuffer::uploadActiveIndices(vk::CommandBuffer cmd)
    {
        if (activeObjectCount == 0) return;

        auto& sf = stagingFrames[currentStagingFrame];

        size_t copySize = activeObjectCount * sizeof(uint32_t);
        std::memcpy(sf.activeIndexStagingMapped, activeObjectIndices.data(), copySize);

        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(sf.activeIndexStagingBuffer, activeIndexBuffer, copyRegion);

        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = activeIndexBuffer;
        barrier.offset = 0;
        barrier.size = copySize;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, {}, barrier, {});
    }
}
