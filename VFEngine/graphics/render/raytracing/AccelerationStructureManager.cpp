#include "AccelerationStructureManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../gpudriven/scene/MergedMeshBuffer.hpp"
#include "print/Log.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::raytracing
{
    AccelerationStructureManager::AccelerationStructureManager(core::Device& device)
        : device(device)
    {
    }

    AccelerationStructureManager::~AccelerationStructureManager()
    {
        cleanup();
    }

    std::string AccelerationStructureManager::makeSubmeshKey(const std::string& meshPath,
                                                              const std::string& submeshName,
                                                              uint32_t submeshIndex)
    {
        return meshPath + "|" + submeshName + "|" + std::to_string(submeshIndex);
    }

    uint64_t AccelerationStructureManager::makeGeometryOffsetKey(uint32_t vertexOffset, uint32_t indexOffset)
    {
        return (static_cast<uint64_t>(vertexOffset) << 32) | static_cast<uint64_t>(indexOffset);
    }

    void AccelerationStructureManager::init()
    {
        if (initialized) return;

        if (!device.isRayQuerySupported())
        {
            vfLogWarning("AccelerationStructureManager: Ray query not supported, skipping init");
            return;
        }

        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
        vfLogInfo("AccelerationStructureManager: Initialized (per-submesh BLAS)");
    }

    void AccelerationStructureManager::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Destroy all BLAS entries
        for (auto& [key, entry] : blasCache)
        {
            destroyBLASEntry(entry);
        }
        blasCache.clear();
        pendingBLASBuilds.clear();
        geometryOffsetToSubmeshKey.clear();

        // Destroy TLAS
        if (tlas)
        {
            vkDevice.destroyAccelerationStructureKHR(tlas);
            tlas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, tlasBuffer, tlasAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceAllocation, device.getMemoryManager());

        // Destroy staging buffers
        for (auto& staging : instanceStagingBuffers)
        {
            core::BufferUtilities::destroyBuffer(vkDevice, staging.buffer, staging.allocation, device.getMemoryManager());
            staging.capacity = 0;
        }

        // Destroy scratch pool
        blasScratchPool.cleanup(device);

        // Destroy descriptor resources
        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (tlasDescriptorLayout)
        {
            vkDevice.destroyDescriptorSetLayout(tlasDescriptorLayout);
            tlasDescriptorLayout = nullptr;
        }

        memoryBudget = {};
        currentInstanceCount = 0;
        instanceBufferCapacity = 0;
        tlasScratchSize = 0;
        tlasBuilt = false;
        initialized = false;
    }

    void AccelerationStructureManager::destroyBLASEntry(BLASEntry& entry)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        if (entry.blas)
        {
            vkDevice.destroyAccelerationStructureKHR(entry.blas);
            entry.blas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, entry.buffer, entry.allocation, device.getMemoryManager());
        memoryBudget.blasTotalBytes -= entry.size;
        memoryBudget.blasCount--;
    }

    void AccelerationStructureManager::notifyMeshReady(const std::string& meshPath,
                                                        const std::string& submeshName,
                                                        uint32_t submeshIndex,
                                                        const gpudriven::SubmeshLocation& submeshLoc)
    {
        if (!initialized) return;

        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);

        // Skip if already built
        if (blasCache.count(key) && blasCache[key].deviceAddress != 0) return;

        const auto& lod0 = submeshLoc.lods[0];
        if (lod0.vertexCount == 0 || lod0.indexCount == 0) return;

        PendingBLAS pending;
        pending.submeshKey = key;
        pending.vertexOffset = lod0.vertexOffset;
        pending.vertexCount = lod0.vertexCount;
        pending.indexOffset = lod0.indexOffset;
        pending.indexCount = lod0.indexCount;
        pendingBLASBuilds.push_back(std::move(pending));

        // Register reverse mapping
        uint64_t offsetKey = makeGeometryOffsetKey(lod0.vertexOffset, lod0.indexOffset);
        geometryOffsetToSubmeshKey[offsetKey] = key;
    }

    void AccelerationStructureManager::notifyMeshRemoved(const std::string& meshPath,
                                                          const std::string& submeshName,
                                                          uint32_t submeshIndex)
    {
        if (!initialized) return;

        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = blasCache.find(key);
        if (it == blasCache.end()) return;

        // Remove reverse mapping
        uint64_t offsetKey = makeGeometryOffsetKey(it->second.lod0VertexOffset, it->second.lod0IndexOffset);
        geometryOffsetToSubmeshKey.erase(offsetKey);

        destroyBLASEntry(it->second);
        blasCache.erase(it);

        // Also remove from pending builds
        pendingBLASBuilds.erase(
            std::remove_if(pendingBLASBuilds.begin(), pendingBLASBuilds.end(),
                [&key](const PendingBLAS& p) { return p.submeshKey == key; }),
            pendingBLASBuilds.end());
    }

    void AccelerationStructureManager::buildPendingBLAS(vk::CommandBuffer cmd,
                                                         vk::Buffer vertexBuffer, uint32_t vertexStride,
                                                         vk::Buffer indexBuffer)
    {
        if (!initialized || pendingBLASBuilds.empty()) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DeviceAddress vertexBufferAddress = vkDevice.getBufferAddress({vertexBuffer});
        vk::DeviceAddress indexBufferAddress = vkDevice.getBufferAddress({indexBuffer});

        for (const auto& pending : pendingBLASBuilds)
        {
            uint32_t triangleCount = pending.indexCount / 3;
            if (triangleCount == 0) continue;

            // Configure triangle geometry with offsets into merged buffers
            vk::AccelerationStructureGeometryTrianglesDataKHR triangleData{};
            triangleData.vertexFormat = vk::Format::eR32G32B32Sfloat;
            triangleData.vertexData.deviceAddress = vertexBufferAddress +
                static_cast<vk::DeviceSize>(pending.vertexOffset) * vertexStride;
            triangleData.vertexStride = vertexStride;
            triangleData.maxVertex = pending.vertexCount - 1;
            triangleData.indexType = vk::IndexType::eUint32;
            triangleData.indexData.deviceAddress = indexBufferAddress +
                static_cast<vk::DeviceSize>(pending.indexOffset) * sizeof(uint32_t);

            vk::AccelerationStructureGeometryKHR geometry{};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.geometry.triangles = triangleData;
            geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

            vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
            buildInfo.geometryCount = 1;
            buildInfo.pGeometries = &geometry;

            // Query size requirements
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
            vkDevice.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                &buildInfo, &triangleCount, &sizeInfo);

            // Destroy old entry if exists
            auto it = blasCache.find(pending.submeshKey);
            if (it != blasCache.end() && it->second.blas)
            {
                destroyBLASEntry(it->second);
            }

            // Create BLAS buffer
            BLASEntry entry{};
            entry.size = sizeInfo.accelerationStructureSize;
            entry.lod0VertexOffset = pending.vertexOffset;
            entry.lod0IndexOffset = pending.indexOffset;
            entry.lod0VertexCount = pending.vertexCount;
            entry.lod0IndexCount = pending.indexCount;

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, entry.buffer, entry.allocation, device.getMemoryManager());
            }

            // Create acceleration structure
            vk::AccelerationStructureCreateInfoKHR createInfo{};
            createInfo.buffer = entry.buffer;
            createInfo.size = sizeInfo.accelerationStructureSize;
            createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            entry.blas = vkDevice.createAccelerationStructureKHR(createInfo);

            // Acquire scratch memory
            vk::DeviceAddress scratchAddress = blasScratchPool.acquire(sizeInfo.buildScratchSize, device);

            // Record build
            buildInfo.dstAccelerationStructure = entry.blas;
            buildInfo.scratchData.deviceAddress = scratchAddress;

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
            rangeInfo.primitiveCount = triangleCount;
            const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
            cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

            // Get device address
            entry.deviceAddress = vkDevice.getAccelerationStructureAddressKHR({entry.blas});

            // Update budget
            memoryBudget.blasTotalBytes += entry.size;
            memoryBudget.blasCount++;

            blasCache[pending.submeshKey] = entry;
        }

        // Single barrier after all BLAS builds
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        memoryBudget.scratchPeakBytes = blasScratchPool.getPeakSize();

        vfLogInfo("AccelerationStructureManager: Built {} BLAS entries (total: {})",
                  pendingBLASBuilds.size(), memoryBudget.blasCount);

        pendingBLASBuilds.clear();
    }

    void AccelerationStructureManager::buildTLAS(vk::CommandBuffer cmd,
                                                  const std::vector<gpudriven::GPUObjectData>& objects,
                                                  uint32_t objectCount,
                                                  const gpudriven::MergedMeshBuffer& mergedBuffer)
    {
        if (!initialized || objectCount == 0 || blasCache.empty()) return;

        vk::Device vkDevice = device.getLogicalDevice();

        // Build instance array
        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        instances.reserve(objectCount);

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            const auto& obj = objects[i];

            // Skip transparent/additive objects - they don't cast RT shadows
            if (obj.flags & (gpudriven::ObjectFlags::Translucent | gpudriven::ObjectFlags::AdditiveBlend))
                continue;

            // Skip terrain tiles - they have their own BLAS path (VK-1151)
            if (obj.flags & gpudriven::ObjectFlags::TerrainTile)
                continue;

            // Look up BLAS for this object via its LOD 0 vertex/index offsets
            uint32_t vertexOffset = obj.lod0Data.x;
            uint32_t indexOffset = obj.lod0Data.y;
            uint64_t offsetKey = makeGeometryOffsetKey(vertexOffset, indexOffset);

            auto keyIt = geometryOffsetToSubmeshKey.find(offsetKey);
            if (keyIt == geometryOffsetToSubmeshKey.end()) continue;

            auto blasIt = blasCache.find(keyIt->second);
            if (blasIt == blasCache.end() || blasIt->second.deviceAddress == 0) continue;

            // Convert glm::mat4 to VkTransformMatrixKHR (3x4 row-major)
            const glm::mat4& m = obj.modelMatrix;
            vk::TransformMatrixKHR transform{};
            transform.matrix[0][0] = m[0][0]; transform.matrix[0][1] = m[1][0]; transform.matrix[0][2] = m[2][0]; transform.matrix[0][3] = m[3][0];
            transform.matrix[1][0] = m[0][1]; transform.matrix[1][1] = m[1][1]; transform.matrix[1][2] = m[2][1]; transform.matrix[1][3] = m[3][1];
            transform.matrix[2][0] = m[0][2]; transform.matrix[2][1] = m[1][2]; transform.matrix[2][2] = m[2][2]; transform.matrix[2][3] = m[3][2];

            vk::AccelerationStructureInstanceKHR inst{};
            inst.transform = transform;
            inst.instanceCustomIndex = i;
            inst.mask = 0xFF;
            inst.instanceShaderBindingTableRecordOffset = 0;
            inst.flags = static_cast<VkGeometryInstanceFlagsKHR>(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            inst.accelerationStructureReference = blasIt->second.deviceAddress;

            instances.push_back(inst);
        }

        if (instances.empty()) return;

        uint32_t instanceCount = static_cast<uint32_t>(instances.size());
        vk::DeviceSize instanceDataSize = sizeof(vk::AccelerationStructureInstanceKHR) * instanceCount;

        // Ensure buffers are large enough
        ensureInstanceBuffer(instanceDataSize);

        auto& staging = instanceStagingBuffers[currentStagingFrame];
        ensureStagingBuffer(staging, instanceDataSize);

        // Upload instance data via staging
        memcpy(staging.allocation.mappedPtr, instances.data(), instanceDataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.size = instanceDataSize;
        cmd.copyBuffer(staging.buffer, instanceBuffer, 1, &copyRegion);

        // Barrier: transfer -> AS build
        vk::MemoryBarrier copyBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &copyBarrier, 0, nullptr, 0, nullptr);

        // Configure TLAS build
        vk::DeviceAddress instanceAddress = vkDevice.getBufferAddress({instanceBuffer});

        vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceAddress;

        vk::AccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        tlasGeometry.geometry.instances = instancesData;

        bool canUpdate = tlasBuilt && (currentInstanceCount == instanceCount);

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                          vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        buildInfo.mode = canUpdate ? vk::BuildAccelerationStructureModeKHR::eUpdate
                                   : vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &tlasGeometry;

        // Query sizes
        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        vkDevice.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo, &instanceCount, &sizeInfo);

        // Rebuild TLAS structure if needed
        if (!canUpdate)
        {
            if (tlas)
            {
                vkDevice.destroyAccelerationStructureKHR(tlas);
                tlas = nullptr;
            }
            core::BufferUtilities::destroyBuffer(vkDevice, tlasBuffer, tlasAllocation, device.getMemoryManager());

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, tlasBuffer, tlasAllocation, device.getMemoryManager());
            }

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{};
            tlasCreateInfo.buffer = tlasBuffer;
            tlasCreateInfo.size = sizeInfo.accelerationStructureSize;
            tlasCreateInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            tlas = vkDevice.createAccelerationStructureKHR(tlasCreateInfo);

            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;

            memoryBudget.tlasTotalBytes = sizeInfo.accelerationStructureSize;
        }

        // Ensure TLAS scratch buffer
        vk::DeviceSize requiredScratch = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);
        ensureTlasScratch(requiredScratch);

        vk::DeviceAddress scratchAddress = vkDevice.getBufferAddress({tlasScratchBuffer});

        if (canUpdate)
        {
            buildInfo.srcAccelerationStructure = tlas;
        }
        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = instanceCount;
        const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

        // Barrier: AS build -> compute/fragment shader reads
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        currentInstanceCount = instanceCount;
        tlasBuilt = true;
        memoryBudget.tlasInstanceCount = instanceCount;

        currentStagingFrame = (currentStagingFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        updateDescriptor();
    }

    void AccelerationStructureManager::ensureInstanceBuffer(vk::DeviceSize requiredSize)
    {
        if (instanceBuffer && instanceBufferCapacity >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceAllocation, device.getMemoryManager());

        // Allocate with some headroom to avoid frequent reallocations
        vk::DeviceSize allocSize = std::max(requiredSize, static_cast<vk::DeviceSize>(requiredSize * 1.5));

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = allocSize;
        request.usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                        vk::BufferUsageFlagBits::eShaderDeviceAddress |
                        vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(request, instanceBuffer, instanceAllocation, device.getMemoryManager());

        instanceBufferCapacity = static_cast<uint32_t>(allocSize);
    }

    void AccelerationStructureManager::ensureStagingBuffer(StagingBuffer& staging, vk::DeviceSize requiredSize)
    {
        if (staging.buffer && staging.capacity >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        core::BufferUtilities::destroyBuffer(vkDevice, staging.buffer, staging.allocation, device.getMemoryManager());

        vk::DeviceSize allocSize = std::max(requiredSize, static_cast<vk::DeviceSize>(requiredSize * 1.5));

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = allocSize;
        request.usage = vk::BufferUsageFlagBits::eTransferSrc;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request, staging.buffer, staging.allocation, device.getMemoryManager());

        staging.capacity = allocSize;
    }

    void AccelerationStructureManager::ensureTlasScratch(vk::DeviceSize requiredSize)
    {
        if (tlasScratchBuffer && tlasScratchSize >= requiredSize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());

        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = requiredSize;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                        vk::BufferUsageFlagBits::eShaderDeviceAddress;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(request, tlasScratchBuffer, tlasScratchAllocation, device.getMemoryManager());

        tlasScratchSize = requiredSize;
    }

    void AccelerationStructureManager::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding tlasBinding{};
        tlasBinding.binding = 0;
        tlasBinding.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &tlasBinding;

        tlasDescriptorLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void AccelerationStructureManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eAccelerationStructureKHR;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void AccelerationStructureManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &tlasDescriptorLayout;

        tlasDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void AccelerationStructureManager::updateDescriptor()
    {
        if (!tlas) return;

        vk::Device vkDevice = device.getLogicalDevice();

        vk::WriteDescriptorSetAccelerationStructureKHR asWrite{};
        asWrite.accelerationStructureCount = 1;
        asWrite.pAccelerationStructures = &tlas;

        vk::WriteDescriptorSet write{};
        write.pNext = &asWrite;
        write.dstSet = tlasDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;

        vkDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }
}
