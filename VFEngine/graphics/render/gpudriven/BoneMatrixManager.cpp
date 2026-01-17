#include "BoneMatrixManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace render::gpudriven {

    BoneMatrixManager::BoneMatrixManager(core::Device& device)
        : device(device)
    {
    }

    BoneMatrixManager::~BoneMatrixManager()
    {
        cleanup();
    }

    void BoneMatrixManager::init()
    {
        if (initialized) {
            return;
        }

        maxBoneMatrices = MAX_BONES_PER_OBJECT * MAX_ANIMATED_OBJECTS;
        cpuBoneMatrices.resize(maxBoneMatrices, glm::mat4(1.0f));
        boneAllocator.reset(maxBoneMatrices);

        loggerInfo("BoneMatrixManager: Initializing with {} max bone matrices ({} MB)",
                   maxBoneMatrices, (maxBoneMatrices * sizeof(glm::mat4)) / (1024 * 1024));

        createBuffers();
        createDescriptorSetLayout();
        createDescriptorPool();
        allocateDescriptorSet();
        updateDescriptor();

        // Initialize GPU buffer with identity matrices
        // This is critical - without this, the GPU buffer contains garbage
        // which causes animated meshes to render with corrupt vertex positions
        initializeGPUBuffer();

        initialized = true;
        loggerInfo("BoneMatrixManager: Initialized successfully, allocator starts at usedCount=0");
    }

    void BoneMatrixManager::cleanup()
    {
        if (!initialized) {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        // Wait for device to be idle before cleanup
        vkDevice.waitIdle();

        // Descriptor resources
        if (descriptorPool) {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (descriptorSetLayout) {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        destroyBuffers();

        allocations.clear();
        dirtyEntities.clear();
        cpuBoneMatrices.clear();

        initialized = false;
        loggerInfo("BoneMatrixManager: Cleaned up");
    }

    void BoneMatrixManager::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = maxBoneMatrices * sizeof(glm::mat4);

        // Create device-local bone SSBO
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, boneBuffer, boneBufferMemory);
        }

        // Create staging buffer (host-visible, persistently mapped)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, stagingBuffer, stagingMemory);

            // Persistently map staging buffer
            stagingMapped = logicalDevice.mapMemory(stagingMemory, 0, bufferSize, vk::MemoryMapFlags{});
        }

        loggerInfo("BoneMatrixManager: Created bone buffers ({} MB each)",
                   bufferSize / (1024 * 1024));
    }

    void BoneMatrixManager::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (stagingMapped) {
            logicalDevice.unmapMemory(stagingMemory);
            stagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, stagingBuffer, stagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, boneBuffer, boneBufferMemory);
    }

    void BoneMatrixManager::initializeGPUBuffer()
    {
        // Copy identity matrices from CPU to staging buffer
        size_t bufferSize = maxBoneMatrices * sizeof(glm::mat4);
        std::memcpy(stagingMapped, cpuBoneMatrices.data(), bufferSize);

        // Use a one-time command buffer to copy from staging to device buffer
        vk::Device vkDevice = device.getLogicalDevice();
        vk::CommandPool cmdPool = device.getStagingCommandPool();

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandPool = cmdPool;
        allocInfo.commandBufferCount = 1;

        vk::CommandBuffer commandBuffer = vkDevice.allocateCommandBuffers(allocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        commandBuffer.begin(beginInfo);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = bufferSize;
        commandBuffer.copyBuffer(stagingBuffer, boneBuffer, copyRegion);

        commandBuffer.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vk::Queue graphicsQueue = device.getGraphicsQueue();
        graphicsQueue.submit(submitInfo, nullptr);
        graphicsQueue.waitIdle();

        vkDevice.freeCommandBuffers(cmdPool, 1, &commandBuffer);

        loggerInfo("BoneMatrixManager: Initialized GPU buffer with identity matrices");
    }

    void BoneMatrixManager::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Single SSBO binding for bone matrices
        vk::DescriptorSetLayoutBinding boneBinding{};
        boneBinding.binding = 0;
        boneBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        boneBinding.descriptorCount = 1;
        boneBinding.stageFlags = vk::ShaderStageFlagBits::eMeshEXT |
                                 vk::ShaderStageFlagBits::eVertex |
                                 vk::ShaderStageFlagBits::eCompute;
        boneBinding.pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &boneBinding;

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        loggerInfo("BoneMatrixManager: Created descriptor set layout");
    }

    void BoneMatrixManager::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
        loggerInfo("BoneMatrixManager: Created descriptor pool");
    }

    void BoneMatrixManager::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        std::vector<vk::DescriptorSet> sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

        loggerInfo("BoneMatrixManager: Allocated descriptor set");
    }

    void BoneMatrixManager::updateDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = boneBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    }

    uint32_t BoneMatrixManager::allocate(entt::entity entity, uint32_t boneCount)
    {
        if (!initialized) {
            loggerError("BoneMatrixManager: Cannot allocate before initialization");
            return INVALID_BONE_OFFSET;
        }

        // Check if already allocated
        auto it = allocations.find(entity);
        if (it != allocations.end()) {
            // Return existing allocation if bone count matches
            if (it->second.boneCount == boneCount) {
                return it->second.boneMatrixOffset;
            }
            // Otherwise free and reallocate
            free(entity);
        }

        if (boneCount == 0 || boneCount > MAX_BONES_PER_OBJECT) {
            loggerWarning("BoneMatrixManager: Invalid bone count {} (max {})",
                          boneCount, MAX_BONES_PER_OBJECT);
            return INVALID_BONE_OFFSET;
        }

        uint32_t offset = boneAllocator.allocate(boneCount);
        if (offset == FreeListAllocator::ALLOCATION_FAILED) {
            loggerError("BoneMatrixManager: Failed to allocate {} bone matrices", boneCount);
            return INVALID_BONE_OFFSET;
        }

        // Mark as used immediately
        boneAllocator.markUsed(boneCount);

        AnimatedObjectBoneData data{};
        data.boneMatrixOffset = offset;
        data.boneCount = boneCount;
        data.dirty = true;

        allocations[entity] = data;

        loggerInfo("BoneMatrixManager: Allocated {} bones at offset {} for entity {}",
                   boneCount, offset, static_cast<uint32_t>(entity));

        return offset;
    }

    void BoneMatrixManager::free(entt::entity entity)
    {
        auto it = allocations.find(entity);
        if (it == allocations.end()) {
            return;
        }

        boneAllocator.free(it->second.boneMatrixOffset, it->second.boneCount);
        allocations.erase(it);

        // Remove from dirty list if present
        auto dirtyIt = std::find(dirtyEntities.begin(), dirtyEntities.end(), entity);
        if (dirtyIt != dirtyEntities.end()) {
            dirtyEntities.erase(dirtyIt);
        }

        loggerInfo("BoneMatrixManager: Freed bone allocation for entity {}",
                   static_cast<uint32_t>(entity));
    }

    bool BoneMatrixManager::hasAllocation(entt::entity entity) const
    {
        return allocations.find(entity) != allocations.end();
    }

    uint32_t BoneMatrixManager::getBoneOffset(entt::entity entity) const
    {
        auto it = allocations.find(entity);
        if (it != allocations.end()) {
            return it->second.boneMatrixOffset;
        }
        return INVALID_BONE_OFFSET;
    }

    void BoneMatrixManager::updateBoneMatrices(entt::entity entity, const std::vector<glm::mat4>& matrices)
    {
        auto it = allocations.find(entity);
        if (it == allocations.end()) {
            loggerWarning("BoneMatrixManager: No allocation for entity {}", static_cast<uint32_t>(entity));
            return;
        }

        AnimatedObjectBoneData& data = it->second;

        if (matrices.size() > data.boneCount) {
            loggerWarning("BoneMatrixManager: Matrix count {} exceeds allocation {} for entity {}",
                          matrices.size(), data.boneCount, static_cast<uint32_t>(entity));
            return;
        }

        // Copy to CPU buffer
        uint32_t offset = data.boneMatrixOffset;
        for (size_t i = 0; i < matrices.size(); ++i) {
            cpuBoneMatrices[offset + i] = matrices[i];
        }

        // Mark as dirty for upload - always add to dirtyEntities if not already present
        // Using dirty flag to avoid duplicate entries within the same frame
        if (!data.dirty) {
            data.dirty = true;
            dirtyEntities.push_back(entity);
        }
        // If already dirty but dirtyEntities was cleared (e.g., init or error),
        // we need to re-add the entity
        else if (std::find(dirtyEntities.begin(), dirtyEntities.end(), entity) == dirtyEntities.end()) {
            dirtyEntities.push_back(entity);
        }
    }

    void BoneMatrixManager::uploadToGPU(vk::CommandBuffer cmd)
    {
        static int callCounter = 0;
        callCounter++;

        // Log occasionally to verify uploadToGPU is being called
        if (callCounter % 300 == 1)
        {
            loggerInfo("BoneMatrixManager: uploadToGPU called (call #{}), initialized={}, dirtyEntities={}",
                callCounter, initialized, dirtyEntities.size());
        }

        if (!initialized || dirtyEntities.empty()) {
            return;
        }

        static int uploadCounter = 0;
        bool shouldLog = (uploadCounter++ % 300 == 0);

        if (shouldLog)
        {
            loggerInfo("BoneMatrixManager: Uploading {} dirty entities to GPU", dirtyEntities.size());
        }

        // Copy all dirty regions to staging buffer
        for (entt::entity entity : dirtyEntities) {
            auto it = allocations.find(entity);
            if (it == allocations.end()) continue;

            AnimatedObjectBoneData& data = it->second;
            uint32_t offset = data.boneMatrixOffset;
            uint32_t count = data.boneCount;

            if (shouldLog)
            {
                loggerInfo("BoneMatrixManager: Uploading entity {} bones: offset={}, count={}",
                    static_cast<uint32_t>(entity), offset, count);
            }

            // Copy to staging buffer
            size_t copyOffset = offset * sizeof(glm::mat4);
            size_t copySize = count * sizeof(glm::mat4);
            std::memcpy(static_cast<char*>(stagingMapped) + copyOffset,
                       &cpuBoneMatrices[offset],
                       copySize);

            data.dirty = false;
        }

        // Copy entire staging buffer to device buffer
        // (Could optimize to copy only dirty regions, but this is simpler)
        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = maxBoneMatrices * sizeof(glm::mat4);
        cmd.copyBuffer(stagingBuffer, boneBuffer, copyRegion);

        // Barrier to ensure copy completes before shader reads
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = boneBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eMeshShaderEXT | vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barrier,
            {}
        );

        dirtyEntities.clear();
    }

}
