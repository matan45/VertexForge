#include "MergedMeshBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Logger.hpp"
#include <material/MaterialInstanceTypes.hpp>
#include <unordered_set>

static_assert(sizeof(resource::Vertex) == 64, "Vertex size must be 64 bytes for MergedMeshBuffer");

namespace render::gpudriven
{
    namespace
    {
        void logFirstVertexBoneData(const std::string& meshPath, const resource::Vertex* vertexData)
        {
            static std::unordered_set<std::string> loggedMeshes;
            if (loggedMeshes.find(meshPath) == loggedMeshes.end())
            {
                const auto& v = vertexData[0];
                loggerInfo("MergedMeshBuffer: First vertex bone data for {}: indices=[{},{},{},{}] weights=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                    meshPath,
                    v.boneIndices.x, v.boneIndices.y, v.boneIndices.z, v.boneIndices.w,
                    v.boneWeights.x, v.boneWeights.y, v.boneWeights.z, v.boneWeights.w);
                loggedMeshes.insert(meshPath);
            }
        }

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
        if (initialized)
        {
            loggerWarning("MergedMeshBuffer already initialized");
            return;
        }

        maxVertexCount = maxVertices;
        maxIndexCount = maxIndices;
        maxObjectCount = MAX_GPU_OBJECTS;

        cpuObjectData.resize(maxObjectCount);

        vertexAllocator.reset(maxVertexCount);
        indexAllocator.reset(maxIndexCount);

        createBuffers();

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    pbrCache.erase(materialPath);
                    instanceToParentCache.erase(materialPath);
                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(pbrCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                        std::erase_if(instanceToParentCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }

        initialized = true;

        loggerInfo("MergedMeshBuffer initialized: {} max vertices, {} max indices, {} max objects",
                   maxVertexCount, maxIndexCount, maxObjectCount);
    }

    void MergedMeshBuffer::cleanup()
    {
        if (!initialized) return;

        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
            materialChangeCallbackId = {};
        }

        pbrCache.clear();
        instanceToParentCache.clear();

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

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxVertexCount * vertexStride;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }
    }

    void MergedMeshBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (objectStagingMapped)
        {
            logicalDevice.unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, objectStagingBuffer, objectStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, objectBuffer, objectBufferMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, indexBuffer, indexBufferMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, vertexBuffer, vertexBufferMemory);
    }

    void MergedMeshBuffer::uploadObjects(vk::CommandBuffer cmd)
    {
        if (currentObjectCount == 0) return;

        if (currentObjectCount > maxObjectCount)
        {
            loggerError("MergedMeshBuffer: object count {} exceeds max {}", currentObjectCount, maxObjectCount);
            return;
        }

        size_t copySize = currentObjectCount * sizeof(GPUObjectData);
        std::memcpy(objectStagingMapped, cpuObjectData.data(), copySize);

        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(objectStagingBuffer, objectBuffer, copyRegion);

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
        if (it != submeshKeyToIndex.end())
        {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    void MergedMeshBuffer::uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount)
    {
        if (!data || vertexCount == 0) return;

        size_t dataSize = vertexCount * vertexStride;
        size_t dstOffset = offset * vertexStride;

        transferManager->copyToBufferAsync(vertexBuffer, data, dataSize, dstOffset);
    }

    void MergedMeshBuffer::uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount)
    {
        if (!data || indexCount == 0) return;

        size_t dataSize = indexCount * sizeof(uint32_t);
        size_t dstOffset = offset * sizeof(uint32_t);

        transferManager->copyToBufferAsync(indexBuffer, data, dataSize, dstOffset);
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
            loggerError("MergedMeshBuffer: Failed to allocate {} vertices for {} LOD{}",
                        vertexCount, meshPath, lodLevel);
            return false;
        }

        uint32_t idxOffset = indexAllocator.allocate(indexCount);
        if (idxOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vertexAllocator.free(vertOffset, vertexCount);
            loggerError("MergedMeshBuffer: Failed to allocate {} indices for {} LOD{}",
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
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end())
        {
            return &registeredMeshes[it->second];
        }

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
                {
                    return nullptr;
                }
            }

            std::string key = makeSubmeshKey(meshPath, submeshStreamInfo.name, subIdx);
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        loggerInfo("MergedMeshBuffer: Reserved space for mesh {} with {} submeshes",
                   meshPath, header.numSubmeshes);
        return &registeredMeshes.back();
    }

    bool MergedMeshBuffer::uploadLOD(const std::string& meshPath,
                                     const std::string& submeshName,
                                     uint32_t submeshIndex,
                                     uint32_t lodLevel,
                                     const LODUploadData& data)
    {
        if (!initialized || lodLevel >= LOD_LEVEL_COUNT)
        {
            return false;
        }

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (!loc)
        {
            loggerError("MergedMeshBuffer::uploadLOD: Submesh not found: {}:{}#{}", meshPath, submeshName,
                        submeshIndex);
            return false;
        }

        const auto& lodInfo = loc->lods[lodLevel];

        if (lodInfo.vertexCount != data.vertexCount || lodInfo.indexCount != data.indexCount)
        {
            loggerError("MergedMeshBuffer::uploadLOD: Count mismatch for {}:{} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        meshPath, submeshName, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        data.vertexCount, data.indexCount);
            return false;
        }

        if (data.vertexData && data.vertexCount > 0)
        {
            logFirstVertexBoneData(meshPath, data.vertexData);
            uploadVertexDataAt(lodInfo.vertexOffset, data.vertexData, data.vertexCount);
        }

        if (data.indexData && data.indexCount > 0)
        {
            uploadIndexDataAt(lodInfo.indexOffset, data.indexData, data.indexCount);
        }

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
        }
    }

    SubmeshLocation* MergedMeshBuffer::getSubmeshLocationMutable(const std::string& meshPath,
                                                                 const std::string& submeshName,
                                                                 uint32_t submeshIndex)
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end())
        {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    void MergedMeshBuffer::flushPendingTransfers()
    {
        if (transferManager && transferManager->hasPendingTransfers())
        {
            transferManager->waitAll();
        }
    }

}
