#include "AccelerationStructureManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../gpudriven/GPUDrivenTypes.hpp"
#include "print/Log.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gi
{
    AccelerationStructureManager::AccelerationStructureManager(core::Device& device)
        : device(device)
    {
    }

    AccelerationStructureManager::~AccelerationStructureManager()
    {
        cleanup();
    }

    void AccelerationStructureManager::init()
    {
        if (initialized)
        {
            return;
        }

        if (!device.isRayQuerySupported())
        {
            vfLogWarning("AccelerationStructureManager: Ray query not supported, skipping init");
            return;
        }

        createDescriptorLayout();
        createDescriptorPool();
        allocateDescriptorSet();

        initialized = true;
        vfLogInfo("AccelerationStructureManager: Initialized");
    }

    void AccelerationStructureManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (tlas)
        {
            vkDevice.destroyAccelerationStructureKHR(tlas);
            tlas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, tlasBuffer, tlasMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceMemory);

        if (blas)
        {
            vkDevice.destroyAccelerationStructureKHR(blas);
            blas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, blasBuffer, blasMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, blasScratchBuffer, blasScratchMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, tlasStagingBuffer, tlasStagingMemory);

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

        blasBuilt = false;
        tlasBuilt = false;
        initialized = false;
    }

    void AccelerationStructureManager::buildBLAS(vk::CommandBuffer cmd,
                                                  vk::Buffer vertexBuffer, uint32_t vertexCount,
                                                  uint32_t vertexStride,
                                                  vk::Buffer indexBuffer, uint32_t indexCount)
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Get buffer device addresses
        vk::BufferDeviceAddressInfo vertexAddrInfo{};
        vertexAddrInfo.buffer = vertexBuffer;
        vk::DeviceAddress vertexAddress = vkDevice.getBufferAddress(vertexAddrInfo);

        vk::BufferDeviceAddressInfo indexAddrInfo{};
        indexAddrInfo.buffer = indexBuffer;
        vk::DeviceAddress indexAddress = vkDevice.getBufferAddress(indexAddrInfo);

        uint32_t triangleCount = indexCount / 3;

        // Geometry description
        vk::AccelerationStructureGeometryTrianglesDataKHR triangleData{};
        triangleData.vertexFormat = vk::Format::eR32G32B32Sfloat; // position is first 3 floats
        triangleData.vertexData.deviceAddress = vertexAddress;
        triangleData.vertexStride = vertexStride;
        triangleData.maxVertex = vertexCount - 1;
        triangleData.indexType = vk::IndexType::eUint32;
        triangleData.indexData.deviceAddress = indexAddress;

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

        // Query sizes
        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        vkDevice.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo, &triangleCount, &sizeInfo);

        // Destroy old BLAS if exists
        if (blas)
        {
            vkDevice.destroyAccelerationStructureKHR(blas);
            blas = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, blasBuffer, blasMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, blasScratchBuffer, blasScratchMemory);

        // Create BLAS buffer
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = sizeInfo.accelerationStructureSize;
            request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, blasBuffer, blasMemory);
        }

        // Create BLAS
        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = blasBuffer;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        blas = vkDevice.createAccelerationStructureKHR(createInfo);

        // Create scratch buffer
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = sizeInfo.buildScratchSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, blasScratchBuffer, blasScratchMemory);
        }

        vk::BufferDeviceAddressInfo scratchAddrInfo{};
        scratchAddrInfo.buffer = blasScratchBuffer;
        vk::DeviceAddress scratchAddress = vkDevice.getBufferAddress(scratchAddrInfo);

        // Build
        buildInfo.dstAccelerationStructure = blas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = triangleCount;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;

        const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

        // Barrier after BLAS build
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        blasBuilt = true;
        vfLogInfo("AccelerationStructureManager: BLAS built ({} triangles)", triangleCount);
    }

    void AccelerationStructureManager::buildTLAS(vk::CommandBuffer cmd,
                                                  const std::vector<gpudriven::GPUObjectData>& objects,
                                                  uint32_t objectCount)
    {
        if (!initialized || !blasBuilt || objectCount == 0)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Get BLAS device address
        vk::AccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
        blasAddrInfo.accelerationStructure = blas;
        vk::DeviceAddress blasAddress = vkDevice.getAccelerationStructureAddressKHR(blasAddrInfo);

        // Build instance data
        std::vector<vk::AccelerationStructureInstanceKHR> instances;
        instances.reserve(objectCount);

        for (uint32_t i = 0; i < objectCount; ++i)
        {
            const auto& obj = objects[i];

            // Convert glm::mat4 to VkTransformMatrixKHR (3x4 row-major)
            vk::TransformMatrixKHR transform{};
            const glm::mat4& m = obj.modelMatrix;
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
            inst.accelerationStructureReference = blasAddress;

            instances.push_back(inst);
        }

        // Destroy old instance buffer if size changed
        if (currentInstanceCount != objectCount)
        {
            core::BufferUtilities::destroyBuffer(vkDevice, instanceBuffer, instanceMemory);
        }

        // Create/update instance buffer
        vk::DeviceSize instanceBufferSize = sizeof(vk::AccelerationStructureInstanceKHR) * objectCount;
        if (!instanceBuffer)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = instanceBufferSize;
            request.usage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress |
                            vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, instanceBuffer, instanceMemory);
        }

        // Destroy previous frame's staging buffer (safe now - previous cmd has completed)
        core::BufferUtilities::destroyBuffer(vkDevice, tlasStagingBuffer, tlasStagingMemory);

        // Upload instance data via staging
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = instanceBufferSize;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, tlasStagingBuffer, tlasStagingMemory);
        }

        void* mapped = vkDevice.mapMemory(tlasStagingMemory, 0, instanceBufferSize);
        memcpy(mapped, instances.data(), instanceBufferSize);
        vkDevice.unmapMemory(tlasStagingMemory);

        vk::BufferCopy copyRegion{};
        copyRegion.size = instanceBufferSize;
        cmd.copyBuffer(tlasStagingBuffer, instanceBuffer, 1, &copyRegion);

        // Barrier after copy
        vk::MemoryBarrier copyBarrier{
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::DependencyFlags{},
            1, &copyBarrier, 0, nullptr, 0, nullptr);

        // Get instance buffer address
        vk::BufferDeviceAddressInfo instanceAddrInfo{};
        instanceAddrInfo.buffer = instanceBuffer;
        vk::DeviceAddress instanceAddress = vkDevice.getBufferAddress(instanceAddrInfo);

        // TLAS geometry
        vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
        instancesData.arrayOfPointers = VK_FALSE;
        instancesData.data.deviceAddress = instanceAddress;

        vk::AccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
        tlasGeometry.geometry.instances = instancesData;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                          vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        buildInfo.mode = tlasBuilt ? vk::BuildAccelerationStructureModeKHR::eUpdate
                                   : vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &tlasGeometry;

        // Query sizes
        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        vkDevice.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo, &objectCount, &sizeInfo);

        // Create TLAS buffer and structure (only on first build or resize)
        if (!tlasBuilt || currentInstanceCount != objectCount)
        {
            if (tlas)
            {
                vkDevice.destroyAccelerationStructureKHR(tlas);
                tlas = nullptr;
            }
            core::BufferUtilities::destroyBuffer(vkDevice, tlasBuffer, tlasMemory);
            core::BufferUtilities::destroyBuffer(vkDevice, tlasScratchBuffer, tlasScratchMemory);

            {
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = sizeInfo.accelerationStructureSize;
                request.usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, tlasBuffer, tlasMemory);
            }

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{};
            tlasCreateInfo.buffer = tlasBuffer;
            tlasCreateInfo.size = sizeInfo.accelerationStructureSize;
            tlasCreateInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            tlas = vkDevice.createAccelerationStructureKHR(tlasCreateInfo);

            // Scratch buffer (use update size if larger)
            {
                vk::DeviceSize scratchSize = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);
                core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
                request.size = scratchSize;
                request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                                vk::BufferUsageFlagBits::eShaderDeviceAddress;
                request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::BufferUtilities::createBuffer(request, tlasScratchBuffer, tlasScratchMemory);
            }

            buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        }

        vk::BufferDeviceAddressInfo scratchAddrInfo{};
        scratchAddrInfo.buffer = tlasScratchBuffer;
        vk::DeviceAddress scratchAddress = vkDevice.getBufferAddress(scratchAddrInfo);

        if (tlasBuilt && currentInstanceCount == objectCount)
        {
            buildInfo.srcAccelerationStructure = tlas;
        }
        buildInfo.dstAccelerationStructure = tlas;
        buildInfo.scratchData.deviceAddress = scratchAddress;

        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = objectCount;
        const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo);

        // Barrier after TLAS build
        vk::MemoryBarrier barrier{
            vk::AccessFlagBits::eAccelerationStructureWriteKHR,
            vk::AccessFlagBits::eAccelerationStructureReadKHR
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);

        currentInstanceCount = objectCount;
        tlasBuilt = true;

        updateDescriptor();

        // Staging buffer kept alive as member - destroyed at start of next buildTLAS or in cleanup
    }

    void AccelerationStructureManager::createDescriptorLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding tlasBinding{};
        tlasBinding.binding = 0;
        tlasBinding.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        tlasBinding.descriptorCount = 1;
        tlasBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

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
        if (!tlas)
        {
            return;
        }

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
