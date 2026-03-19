#include "MergedMeshBuffer.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Log.hpp"
#include <material/MaterialInstanceTypes.hpp>
#include <algorithm>

static_assert(sizeof(resource::Vertex) == 64, "Vertex size must be 64 bytes for MergedMeshBuffer");

namespace render::gpudriven
{
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
        if (initialized) return;

        maxVertexCount = maxVertices;
        maxIndexCount = maxIndices;
        maxObjectCount = MAX_GPU_OBJECTS;

        cpuObjectData.resize(maxObjectCount);
        cpuInstanceTransforms.resize(maxInstanceCount);

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

        vfLogInfo("MergedMeshBuffer initialized: {} max vertices, {} max indices, {} max objects",
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
        cpuInstanceTransforms.clear();
        activeObjectIndices.clear();
        entityToSlot.clear();
        dirtySlots.clear();

        totalVertexCount = 0;
        totalIndexCount = 0;
        currentObjectCount = 0;
        currentInstanceCount = 0;
        activeObjectCount = 0;
        persistentMode = false;
        initialized = false;
    }

    void MergedMeshBuffer::createBuffers()
    {
        createGeometryBuffers();
        createObjectBuffers();
        createInstanceBuffers();
        createActiveIndexBuffers();
    }

    void MergedMeshBuffer::createGeometryBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxVertexCount * vertexStride;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eShaderDeviceAddress |
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eShaderDeviceAddress |
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }
    }

    void MergedMeshBuffer::createObjectBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

            objectStagingMapped = logicalDevice.mapMemory(objectStagingMemory, 0, request.size, vk::MemoryMapFlags{});
        }
    }

    void MergedMeshBuffer::createInstanceBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxInstanceCount * sizeof(GPUInstanceTransform);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, instanceTransformBuffer, instanceTransformBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxInstanceCount * sizeof(GPUInstanceTransform);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, instanceStagingBuffer, instanceStagingMemory);

            instanceStagingMapped = logicalDevice.mapMemory(instanceStagingMemory, 0, request.size, vk::MemoryMapFlags{});
        }
    }

    void MergedMeshBuffer::createActiveIndexBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, activeIndexBuffer, activeIndexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, activeIndexStagingBuffer, activeIndexStagingMemory);

            activeIndexStagingMapped = logicalDevice.mapMemory(activeIndexStagingMemory, 0, request.size, vk::MemoryMapFlags{});
        }

        activeObjectIndices.resize(maxObjectCount);
    }

    void MergedMeshBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (activeIndexStagingMapped) { logicalDevice.unmapMemory(activeIndexStagingMemory); activeIndexStagingMapped = nullptr; }
        core::BufferUtilities::destroyBuffer(logicalDevice, activeIndexStagingBuffer, activeIndexStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, activeIndexBuffer, activeIndexBufferMemory);

        if (instanceStagingMapped) { logicalDevice.unmapMemory(instanceStagingMemory); instanceStagingMapped = nullptr; }
        core::BufferUtilities::destroyBuffer(logicalDevice, instanceStagingBuffer, instanceStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, instanceTransformBuffer, instanceTransformBufferMemory);

        if (objectStagingMapped) { logicalDevice.unmapMemory(objectStagingMemory); objectStagingMapped = nullptr; }
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
            vfLogError("MergedMeshBuffer: object count {} exceeds max {}", currentObjectCount, maxObjectCount);
            return;
        }

        peakObjectCount = std::max(peakObjectCount, currentObjectCount);

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

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                            {}, {}, barrier, {});

        // Upload identity active-index mapping for legacy (edit) mode
        {
            auto* indices = static_cast<uint32_t*>(activeIndexStagingMapped);
            for (uint32_t i = 0; i < currentObjectCount; ++i)
                indices[i] = i;

            vk::BufferCopy idxCopy;
            idxCopy.srcOffset = 0;
            idxCopy.dstOffset = 0;
            idxCopy.size = currentObjectCount * sizeof(uint32_t);
            cmd.copyBuffer(activeIndexStagingBuffer, activeIndexBuffer, idxCopy);

            vk::BufferMemoryBarrier idxBarrier;
            idxBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            idxBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            idxBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            idxBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            idxBarrier.buffer = activeIndexBuffer;
            idxBarrier.offset = 0;
            idxBarrier.size = idxCopy.size;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                                {}, {}, idxBarrier, {});
        }
    }

    void MergedMeshBuffer::uploadInstances(vk::CommandBuffer cmd)
    {
        if (currentInstanceCount == 0) return;

        if (currentInstanceCount > maxInstanceCount)
        {
            vfLogError("MergedMeshBuffer: instance count {} exceeds max {}", currentInstanceCount, maxInstanceCount);
            return;
        }

        size_t copySize = currentInstanceCount * sizeof(GPUInstanceTransform);
        std::memcpy(instanceStagingMapped, cpuInstanceTransforms.data(), copySize);

        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(instanceStagingBuffer, instanceTransformBuffer, copyRegion);

        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = instanceTransformBuffer;
        barrier.offset = 0;
        barrier.size = copySize;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eComputeShader,
                            {}, {}, barrier, {});
    }

    const SubmeshLocation* MergedMeshBuffer::getSubmeshLocation(const std::string& meshPath,
                                                                const std::string& submeshName,
                                                                uint32_t submeshIndex) const
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end())
            return &allSubmeshLocations[it->second];
        return nullptr;
    }

    void MergedMeshBuffer::flushPendingTransfers()
    {
        if (transferManager && transferManager->hasPendingTransfers())
            transferManager->waitAll();
    }
}
