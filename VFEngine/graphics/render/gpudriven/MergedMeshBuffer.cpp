#include "MergedMeshBuffer.hpp"
#include "../mesh/MeshMetadataCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Logger.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

// Verify vertex stride matches actual Vertex struct
static_assert(sizeof(resource::Vertex) == 32, "Vertex size must be 32 bytes for MergedMeshBuffer");

namespace render::gpudriven {

    MergedMeshBuffer::MergedMeshBuffer(core::Device& device)
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

    MergedMeshBuffer::~MergedMeshBuffer()
    {
        cleanup();
    }

    void MergedMeshBuffer::init(uint32_t maxVertices, uint32_t maxIndices)
    {
        if (initialized) {
            loggerWarning("MergedMeshBuffer already initialized");
            return;
        }

        maxVertexCount = maxVertices;
        maxIndexCount = maxIndices;
        maxObjectCount = MAX_GPU_OBJECTS;

        cpuObjectData.resize(maxObjectCount);

        createBuffers();
        initialized = true;

        loggerInfo("MergedMeshBuffer initialized: {} max vertices, {} max indices, {} max objects",
                   maxVertexCount, maxIndexCount, maxObjectCount);
    }

    void MergedMeshBuffer::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();
        destroyBuffers();

        registeredMeshes.clear();
        meshPathToIndex.clear();
        allSubmeshLocations.clear();
        submeshKeyToIndex.clear();
        cpuObjectData.clear();

        totalVertexCount = 0;
        totalIndexCount = 0;
        currentObjectCount = 0;
        initialized = false;

        loggerInfo("MergedMeshBuffer cleaned up");
    }

    void MergedMeshBuffer::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Create merged vertex buffer (device-local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxVertexCount * vertexStride;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eStorageBuffer; // For compute access if needed
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        // Create merged index buffer (device-local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }

        // Create object buffer (device-local storage buffer)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        // Create staging buffer for object updates (host-visible, persistently mapped)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

            // Persistently map staging buffer
            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }
    }

    void MergedMeshBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (objectStagingMapped) {
            logicalDevice.unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }

        if (objectStagingBuffer) {
            logicalDevice.destroyBuffer(objectStagingBuffer);
            logicalDevice.freeMemory(objectStagingMemory);
            objectStagingBuffer = nullptr;
        }

        if (objectBuffer) {
            logicalDevice.destroyBuffer(objectBuffer);
            logicalDevice.freeMemory(objectBufferMemory);
            objectBuffer = nullptr;
        }

        if (indexBuffer) {
            logicalDevice.destroyBuffer(indexBuffer);
            logicalDevice.freeMemory(indexBufferMemory);
            indexBuffer = nullptr;
        }

        if (vertexBuffer) {
            logicalDevice.destroyBuffer(vertexBuffer);
            logicalDevice.freeMemory(vertexBufferMemory);
            vertexBuffer = nullptr;
        }
    }

    void MergedMeshBuffer::unregisterMesh(const std::string& meshPath)
    {
        auto it = meshPathToIndex.find(meshPath);
        if (it == meshPathToIndex.end()) {
            return;
        }

        // Note: This doesn't reclaim space, just marks as unused
        // A full rebuild would be needed to defragment
        const auto& meshInfo = registeredMeshes[it->second];

        for (const auto& submesh : meshInfo.submeshes) {
            std::string key = makeSubmeshKey(meshPath, submesh.submeshName, submesh.submeshIndex);
            submeshKeyToIndex.erase(key);
        }

        // Mark entry as invalid (empty path)
        registeredMeshes[it->second].meshPath.clear();
        meshPathToIndex.erase(it);

        dirty = true;
        loggerInfo("Unregistered mesh from MergedMeshBuffer: {}", meshPath);
    }

    void MergedMeshBuffer::uploadObjects(vk::CommandBuffer cmd)
    {
        if (currentObjectCount == 0) return;

        // Copy CPU data to staging buffer
        size_t copySize = currentObjectCount * sizeof(GPUObjectData);
        std::memcpy(objectStagingMapped, cpuObjectData.data(), copySize);

        // Copy from staging to device-local buffer
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(objectStagingBuffer, objectBuffer, copyRegion);

        // Barrier to ensure copy completes before compute shader reads
        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = objectBuffer;
        barrier.offset = 0;
        barrier.size = copySize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barrier,
            {}
        );
    }

    const SubmeshLocation* MergedMeshBuffer::getSubmeshLocation(const std::string& meshPath,
                                                                 const std::string& submeshName,
                                                                 uint32_t submeshIndex) const
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end()) {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    void MergedMeshBuffer::resizeObjectBuffer(uint32_t newMaxObjects)
    {
        if (newMaxObjects <= maxObjectCount) return;

        device.getLogicalDevice().waitIdle();

        // Destroy old buffers
        if (objectStagingMapped) {
            device.getLogicalDevice().unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }
        if (objectStagingBuffer) {
            device.getLogicalDevice().destroyBuffer(objectStagingBuffer);
            device.getLogicalDevice().freeMemory(objectStagingMemory);
        }
        if (objectBuffer) {
            device.getLogicalDevice().destroyBuffer(objectBuffer);
            device.getLogicalDevice().freeMemory(objectBufferMemory);
        }

        maxObjectCount = newMaxObjects;
        cpuObjectData.resize(maxObjectCount);

        // Recreate buffers
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);
            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }

        loggerInfo("MergedMeshBuffer: resized object buffer to {} objects", maxObjectCount);
    }

    // ===== STREAMING SUPPORT IMPLEMENTATION =====

    uint32_t MergedMeshBuffer::allocateVertexSpace(uint32_t count) {
        if (count == 0) return 0;

        // First try to find a free block that fits
        for (auto it = vertexFreeList.begin(); it != vertexFreeList.end(); ++it) {
            if (it->size >= count) {
                uint32_t offset = it->offset;
                if (it->size == count) {
                    vertexFreeList.erase(it);
                } else {
                    it->offset += count;
                    it->size -= count;
                }
                return offset;
            }
        }

        // No free block found, allocate from end if space available
        if (totalVertexCount + reservedVertexCount + count <= maxVertexCount) {
            uint32_t offset = totalVertexCount + reservedVertexCount;
            reservedVertexCount += count;
            return offset;
        }

        return UINT32_MAX; // Allocation failed
    }

    uint32_t MergedMeshBuffer::allocateIndexSpace(uint32_t count) {
        if (count == 0) return 0;

        // First try to find a free block that fits
        for (auto it = indexFreeList.begin(); it != indexFreeList.end(); ++it) {
            if (it->size >= count) {
                uint32_t offset = it->offset;
                if (it->size == count) {
                    indexFreeList.erase(it);
                } else {
                    it->offset += count;
                    it->size -= count;
                }
                return offset;
            }
        }

        // No free block found, allocate from end if space available
        if (totalIndexCount + reservedIndexCount + count <= maxIndexCount) {
            uint32_t offset = totalIndexCount + reservedIndexCount;
            reservedIndexCount += count;
            return offset;
        }

        return UINT32_MAX; // Allocation failed
    }

    void MergedMeshBuffer::freeVertexSpace(uint32_t offset, uint32_t count) {
        if (count == 0) return;
        vertexFreeList.push_back({offset, count});
        defragmentFreeList(vertexFreeList);
    }

    void MergedMeshBuffer::freeIndexSpace(uint32_t offset, uint32_t count) {
        if (count == 0) return;
        indexFreeList.push_back({offset, count});
        defragmentFreeList(indexFreeList);
    }

    void MergedMeshBuffer::defragmentFreeList(std::vector<FreeBlock>& freeList) {
        if (freeList.size() < 2) return;

        // Sort by offset
        std::sort(freeList.begin(), freeList.end(),
            [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

        // Merge adjacent blocks
        std::vector<FreeBlock> merged;
        merged.push_back(freeList[0]);

        for (size_t i = 1; i < freeList.size(); ++i) {
            FreeBlock& last = merged.back();
            const FreeBlock& current = freeList[i];

            if (last.offset + last.size == current.offset) {
                // Adjacent, merge
                last.size += current.size;
            } else {
                merged.push_back(current);
            }
        }

        freeList = std::move(merged);
    }

    void MergedMeshBuffer::uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount) {
        if (!data || vertexCount == 0) return;

        size_t dataSize = vertexCount * vertexStride;
        size_t dstOffset = offset * vertexStride;

        transferManager->copyToBufferAsync(vertexBuffer, data, dataSize, dstOffset);
    }

    void MergedMeshBuffer::uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount) {
        if (!data || indexCount == 0) return;

        size_t dataSize = indexCount * sizeof(uint32_t);
        size_t dstOffset = offset * sizeof(uint32_t);

        transferManager->copyToBufferAsync(indexBuffer, data, dataSize, dstOffset);
    }

    MergedMeshInfo* MergedMeshBuffer::reserveMesh(const std::string& meshPath,
                                                   const resource::MeshStreamHeader& header) {
        if (!initialized) {
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        // Check if already registered
        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end()) {
            return &registeredMeshes[it->second];
        }

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        // Reserve space for all submeshes and LODs
        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx) {
            const auto& submeshStreamInfo = header.submeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = submeshStreamInfo.name;
            loc.submeshIndex = subIdx;

            // Initialize bounding box (will be computed when LOD0 is uploaded)
            loc.aabbMin = glm::vec3(0.0f);
            loc.aabbMax = glm::vec3(0.0f);
            loc.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

            // Allocate space for each LOD level
            for (uint32_t lodIdx = 0; lodIdx < LOD_LEVEL_COUNT; ++lodIdx) {
                const auto& lodFileInfo = submeshStreamInfo.lods[lodIdx];
                LODDrawInfo& lodInfo = loc.lods[lodIdx];

                if (lodFileInfo.vertexCount > 0) {
                    // Allocate vertex space
                    uint32_t vertOffset = allocateVertexSpace(lodFileInfo.vertexCount);
                    if (vertOffset == UINT32_MAX) {
                        loggerError("MergedMeshBuffer: Failed to allocate {} vertices for {} LOD{}",
                                    lodFileInfo.vertexCount, meshPath, lodIdx);
                        return nullptr;
                    }

                    // Allocate index space
                    uint32_t idxOffset = allocateIndexSpace(lodFileInfo.indexCount);
                    if (idxOffset == UINT32_MAX) {
                        // Rollback vertex allocation
                        freeVertexSpace(vertOffset, lodFileInfo.vertexCount);
                        loggerError("MergedMeshBuffer: Failed to allocate {} indices for {} LOD{}",
                                    lodFileInfo.indexCount, meshPath, lodIdx);
                        return nullptr;
                    }

                    lodInfo.vertexOffset = vertOffset;
                    lodInfo.indexOffset = idxOffset;
                    lodInfo.vertexCount = lodFileInfo.vertexCount;
                    lodInfo.indexCount = lodFileInfo.indexCount;

                    // Mark as not yet uploaded
                    loc.lodStates[lodIdx] = LODStreamState::NotRequested;
                } else if (lodIdx > 0) {
                    // Copy from previous LOD
                    lodInfo = loc.lods[lodIdx - 1];
                    loc.lodStates[lodIdx] = loc.lodStates[lodIdx - 1];
                } else {
                    lodInfo = {0, 0, 0, 0};
                    loc.lodStates[lodIdx] = LODStreamState::NotRequested;
                }
            }

            // Store submesh location (include index for uniqueness when names are duplicated)
            std::string key = makeSubmeshKey(meshPath, submeshStreamInfo.name, subIdx);
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        // Store mesh info
        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        dirty = true;
        loggerInfo("MergedMeshBuffer: Reserved space for mesh {} with {} submeshes",
                    meshPath, header.numSubmeshes);
        return &registeredMeshes.back();
    }

    bool MergedMeshBuffer::uploadLOD(const std::string& meshPath,
                                      const std::string& submeshName,
                                      uint32_t submeshIndex,
                                      uint32_t lodLevel,
                                      const resource::Vertex* vertexData, uint32_t vertexCount,
                                      const uint32_t* indexData, uint32_t indexCount) {
        if (!initialized || lodLevel >= LOD_LEVEL_COUNT) {
            return false;
        }

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (!loc) {
            loggerError("MergedMeshBuffer::uploadLOD: Submesh not found: {}:{}#{}", meshPath, submeshName, submeshIndex);
            return false;
        }

        const auto& lodInfo = loc->lods[lodLevel];

        // Validate counts match reservation
        if (lodInfo.vertexCount != vertexCount || lodInfo.indexCount != indexCount) {
            loggerError("MergedMeshBuffer::uploadLOD: Count mismatch for {}:{} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        meshPath, submeshName, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        vertexCount, indexCount);
            return false;
        }

        // Upload vertex data
        if (vertexData && vertexCount > 0) {
            uploadVertexDataAt(lodInfo.vertexOffset, vertexData, vertexCount);
        }

        // Upload index data
        if (indexData && indexCount > 0) {
            uploadIndexDataAt(lodInfo.indexOffset, indexData, indexCount);
        }

        // Update state to uploading
        loc->lodStates[lodLevel] = LODStreamState::Uploading;

        // Compute bounding box from first LOD that uploads (any LOD works, earlier is better for culling)
        // Only update if not already computed (aabbMin == aabbMax == 0 is uninitialized)
        bool boundsNotComputed = (loc->aabbMin == glm::vec3(0.0f) && loc->aabbMax == glm::vec3(0.0f));
        if (vertexData && vertexCount > 0 && boundsNotComputed) {
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

            for (uint32_t i = 0; i < vertexCount; ++i) {
                const auto& v = vertexData[i];
                minBounds = glm::min(minBounds, v.position);
                maxBounds = glm::max(maxBounds, v.position);
            }

            loc->aabbMin = minBounds;
            loc->aabbMax = maxBounds;
            loc->calculateBoundingSphere();
        }

        return true;
    }

    void MergedMeshBuffer::markLODReady(const std::string& meshPath,
                                         const std::string& submeshName,
                                         uint32_t submeshIndex,
                                         uint32_t lodLevel) {
        if (lodLevel >= LOD_LEVEL_COUNT) return;

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (loc) {
            loc->lodStates[lodLevel] = LODStreamState::Ready;

            // Update totalVertexCount/totalIndexCount if this was reserved space
            // This is approximate - for precise tracking we'd need per-LOD upload flags
            if (reservedVertexCount >= loc->lods[lodLevel].vertexCount) {
                reservedVertexCount -= loc->lods[lodLevel].vertexCount;
                totalVertexCount += loc->lods[lodLevel].vertexCount;
            }
            if (reservedIndexCount >= loc->lods[lodLevel].indexCount) {
                reservedIndexCount -= loc->lods[lodLevel].indexCount;
                totalIndexCount += loc->lods[lodLevel].indexCount;
            }

            dirty = true;
        }
    }

    bool MergedMeshBuffer::hasRenderableData(const std::string& meshPath) const {
        auto it = meshPathToIndex.find(meshPath);
        if (it == meshPathToIndex.end()) return false;

        const auto& meshInfo = registeredMeshes[it->second];
        for (const auto& submesh : meshInfo.submeshes) {
            if (submesh.hasRenderableLOD()) {
                return true;
            }
        }
        return false;
    }

    SubmeshLocation* MergedMeshBuffer::getSubmeshLocationMutable(const std::string& meshPath,
                                                                   const std::string& submeshName,
                                                                   uint32_t submeshIndex) {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end()) {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    MergedMeshBuffer::StreamingStats MergedMeshBuffer::getStreamingStats() const {
        StreamingStats stats;
        stats.totalMeshes = static_cast<uint32_t>(registeredMeshes.size());
        stats.usedVertexBytes = totalVertexCount * vertexStride;
        stats.usedIndexBytes = totalIndexCount * sizeof(uint32_t);
        stats.reservedVertexBytes = reservedVertexCount * vertexStride;
        stats.reservedIndexBytes = reservedIndexCount * sizeof(uint32_t);

        for (const auto& meshInfo : registeredMeshes) {
            if (meshInfo.meshPath.empty()) continue; // Unregistered

            bool hasRenderable = false;
            for (const auto& submesh : meshInfo.submeshes) {
                for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
                    if (submesh.lodStates[lod] == LODStreamState::Ready) {
                        stats.totalLODsReady++;
                        hasRenderable = true;
                    } else if (submesh.lodStates[lod] != LODStreamState::NotRequested) {
                        stats.totalLODsPending++;
                    }
                }
            }
            if (hasRenderable) {
                stats.meshesWithRenderableData++;
            }
        }

        return stats;
    }

    // ===== METADATA-BASED REGISTRATION (for streaming path) =====

    MergedMeshInfo* MergedMeshBuffer::registerMeshFromMetadata(const std::string& meshPath,
                                                                 const mesh::MeshMetadata& metadata)
    {
        if (!initialized) {
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        // Check if already registered
        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end()) {
            return &registeredMeshes[it->second];
        }

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        // Reserve space for all submeshes
        for (size_t subIdx = 0; subIdx < metadata.subMeshes.size(); ++subIdx) {
            const auto& subMeta = metadata.subMeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = subMeta.name;
            loc.submeshIndex = static_cast<uint32_t>(subIdx);

            // Copy bounding box from metadata
            loc.aabbMin = subMeta.boundingBox.min;
            loc.aabbMax = subMeta.boundingBox.max;
            loc.calculateBoundingSphere();

            // Validate bounding sphere
            if (loc.boundingSphere.w <= 0.0f || std::isnan(loc.boundingSphere.w) || std::isinf(loc.boundingSphere.w)) {
                // Bounding box may not be set yet (header-only parse), use default
                loc.boundingSphere.w = 1.0f;
            }

            // Allocate space for each LOD level
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
                const auto& lodMeta = subMeta.lodLevels[lod];
                LODDrawInfo& lodInfo = loc.lods[lod];

                if (lodMeta.vertexCount > 0) {
                    // Allocate vertex space
                    uint32_t vertOffset = allocateVertexSpace(lodMeta.vertexCount);
                    if (vertOffset == UINT32_MAX) {
                        loggerError("MergedMeshBuffer: Failed to allocate {} vertices for {} LOD{}",
                                    lodMeta.vertexCount, meshPath, lod);
                        return nullptr;
                    }

                    // Allocate index space
                    uint32_t idxOffset = allocateIndexSpace(lodMeta.indexCount);
                    if (idxOffset == UINT32_MAX) {
                        freeVertexSpace(vertOffset, lodMeta.vertexCount);
                        loggerError("MergedMeshBuffer: Failed to allocate {} indices for {} LOD{}",
                                    lodMeta.indexCount, meshPath, lod);
                        return nullptr;
                    }

                    lodInfo.vertexOffset = vertOffset;
                    lodInfo.indexOffset = idxOffset;
                    lodInfo.vertexCount = lodMeta.vertexCount;
                    lodInfo.indexCount = lodMeta.indexCount;

                    loc.lodStates[lod] = LODStreamState::NotRequested;
                } else if (lod > 0) {
                    // Copy from previous LOD
                    lodInfo = loc.lods[lod - 1];
                    loc.lodStates[lod] = loc.lodStates[lod - 1];
                } else {
                    lodInfo = {0, 0, 0, 0};
                    loc.lodStates[lod] = LODStreamState::NotRequested;
                }
            }

            // Update mesh bounding box
            if (meshInfo.submeshCount == 0) {
                meshInfo.aabbMin = loc.aabbMin;
                meshInfo.aabbMax = loc.aabbMax;
            } else {
                meshInfo.aabbMin = glm::min(meshInfo.aabbMin, loc.aabbMin);
                meshInfo.aabbMax = glm::max(meshInfo.aabbMax, loc.aabbMax);
            }

            // Store submesh location
            std::string key = makeSubmeshKey(meshPath, subMeta.name, static_cast<uint32_t>(subIdx));
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        // Store mesh info
        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        dirty = true;
        loggerInfo("MergedMeshBuffer: Reserved space for mesh {} ({} submeshes, metadata path)",
                    meshPath, metadata.subMeshes.size());
        return &registeredMeshes.back();
    }

    void MergedMeshBuffer::updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                                          const TextureIndexResolver& textureResolver,
                                          float time)
    {
        currentObjectCount = 0;

        for (const auto& meshRender : renderData) {
            // Get submesh locations for this mesh
            auto meshIt = meshPathToIndex.find(meshRender.meshPath);
            if (meshIt == meshPathToIndex.end()) {
                // Mesh not registered - skip silently (streaming may not have loaded it yet)
                continue;
            }

            const auto& meshInfo = registeredMeshes[meshIt->second];

            // Create one GPUObjectData per submesh
            for (uint32_t subIdx = 0; subIdx < meshInfo.submeshCount; ++subIdx) {
                const auto& submeshLoc = allSubmeshLocations[meshInfo.firstSubmeshIndex + subIdx];

                // Skip submeshes with no renderable LODs
                if (!submeshLoc.hasRenderableLOD()) {
                    continue;
                }

                if (currentObjectCount >= maxObjectCount) {
                    loggerWarning("MergedMeshBuffer: max object count reached");
                    break;
                }

                GPUObjectData& obj = cpuObjectData[currentObjectCount];

                // Transform
                obj.modelMatrix = meshRender.modelMatrix;

                // Bounding volumes
                obj.boundingSphere = submeshLoc.boundingSphere;

                // LOD data
                obj.lod0Data = glm::uvec4(
                    submeshLoc.lods[0].vertexOffset,
                    submeshLoc.lods[0].indexOffset,
                    submeshLoc.lods[0].indexCount,
                    submeshLoc.lods[0].vertexCount
                );
                obj.lod1Data = glm::uvec4(
                    submeshLoc.lods[1].vertexOffset,
                    submeshLoc.lods[1].indexOffset,
                    submeshLoc.lods[1].indexCount,
                    submeshLoc.lods[1].vertexCount
                );
                obj.lod2Data = glm::uvec4(
                    submeshLoc.lods[2].vertexOffset,
                    submeshLoc.lods[2].indexOffset,
                    submeshLoc.lods[2].indexCount,
                    submeshLoc.lods[2].vertexCount
                );
                obj.lod3Data = glm::uvec4(
                    submeshLoc.lods[3].vertexOffset,
                    submeshLoc.lods[3].indexOffset,
                    submeshLoc.lods[3].indexCount,
                    submeshLoc.lods[3].vertexCount
                );

                // LOD thresholds
                float bias = meshRender.lodBias;
                obj.lodThresholds = glm::vec4(
                    LOD_THRESHOLD_0 * std::pow(2.0f, -bias),
                    LOD_THRESHOLD_1 * std::pow(2.0f, -bias),
                    LOD_THRESHOLD_2 * std::pow(2.0f, -bias),
                    bias
                );

                // Material data
                const auto* subMat = meshRender.getMaterialForSubmesh(submeshLoc.submeshName);
                float emissionStrength = 0.0f;
                std::string materialPath;

                if (subMat) {
                    obj.albedo = subMat->albedo;
                    emissionStrength = subMat->emission;
                    obj.iblParams = glm::vec4(subMat->iblDiffuse, subMat->iblSpecular, 0.0f, 0.0f);
                    materialPath = subMat->materialPath;
                    obj.materialParams = glm::vec4(
                        subMat->metallic,
                        subMat->roughness,
                        subMat->ao,
                        emissionStrength
                    );
                } else {
                    obj.albedo = meshRender.albedo;
                    emissionStrength = meshRender.emission;
                    obj.iblParams = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);
                    materialPath = meshRender.defaultMaterialPath;
                    obj.materialParams = glm::vec4(
                        meshRender.metallic,
                        meshRender.roughness,
                        meshRender.ao,
                        emissionStrength
                    );
                }

                // Evaluate dynamic emission
                if (!materialPath.empty() && time > 0.0f) {
                    auto matData = resource::ResourceManager::getMaterial(materialPath);
                    if (matData) {
                        const auto* outputNode = matData->graph.findOutputNode();
                        if (outputNode) {
                            float dynamicEmission = mesh::MaterialPBRExtractor::evaluateEmissionStrength(
                                matData->graph, outputNode->id, time);
                            if (dynamicEmission != 0.0f) {
                                obj.materialParams.w = dynamicEmission;
                            }
                        }
                    }
                }

                // Texture indices
                if (textureResolver && !materialPath.empty()) {
                    obj.textureIndices0 = glm::uvec4(
                        textureResolver(materialPath, TextureSlotType::Albedo),
                        textureResolver(materialPath, TextureSlotType::Normal),
                        textureResolver(materialPath, TextureSlotType::ORM),
                        textureResolver(materialPath, TextureSlotType::Metallic)
                    );
                    obj.textureIndices1 = glm::uvec4(
                        textureResolver(materialPath, TextureSlotType::Roughness),
                        textureResolver(materialPath, TextureSlotType::AO),
                        textureResolver(materialPath, TextureSlotType::Emission),
                        textureResolver(materialPath, TextureSlotType::Height)
                    );
                } else {
                    obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
                    obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
                }

                // Flags
                obj.flags = ObjectFlags::Visible | ObjectFlags::CastShadow | ObjectFlags::ReceiveShadow;
                if (subMat && subMat->blendMode == 1) {
                    obj.flags |= ObjectFlags::AlphaMask;
                }

                // Uniform scale check
                float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
                float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
                float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
                constexpr float uniformScaleEpsilon = 0.001f;
                if (std::abs(scaleX - scaleY) < uniformScaleEpsilon &&
                    std::abs(scaleY - scaleZ) < uniformScaleEpsilon) {
                    obj.flags |= ObjectFlags::UniformScale;
                }

                obj.entityId = currentObjectCount;
                obj.availableLODMask = submeshLoc.getAvailableLODMask();

                currentObjectCount++;
            }
        }
    }

}
