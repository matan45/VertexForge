#include "OcclusionCullingManager.hpp"
#include "HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::occlusion
{
    OcclusionCullingManager::OcclusionCullingManager(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    OcclusionCullingManager::~OcclusionCullingManager()
    {
        cleanup();
    }

    void OcclusionCullingManager::init(HiZBuffer* hiZ)
    {
        hiZBuffer = hiZ;

        createBuffers(INITIAL_MAX_OBJECTS);
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        loggerInfo("Occlusion culling manager initialized with max {} objects", INITIAL_MAX_OBJECTS);
    }

    void OcclusionCullingManager::createBuffers(uint32_t maxObjects)
    {
        maxObjectCount = maxObjects;
        
        core::BufferInfoRequest objectRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(GPUObjectData) * maxObjects,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::Utilities::createBuffer(objectRequest, objectBuffer, objectBufferMemory);
        
        core::BufferInfoRequest visRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(uint32_t) * maxObjects,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::Utilities::createBuffer(visRequest, visibilityBuffer, visibilityBufferMemory);
        
        core::BufferInfoRequest cameraRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(CullCameraData),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::Utilities::createBuffer(cameraRequest, cameraBuffer, cameraBufferMemory);
        
        core::BufferInfoRequest stagingRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(uint32_t) * maxObjects,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingBufferMemory);

        needsDescriptorUpdate = true;
    }

    void OcclusionCullingManager::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/hiz/hiz_cull.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("Failed to load occlusion culling shader: {}", shader->getLastCompilationError());
            return;
        }
        
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
        
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
        
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;
        
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;
        
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
        
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);
        
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto pipelineResult = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        cullPipeline = pipelineResult.value;
    }

    void OcclusionCullingManager::createDescriptorSets()
    {
        std::array<vk::DescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 2;
        poolSizes[2].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[2].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        needsDescriptorUpdate = true;
    }

    void OcclusionCullingManager::resizeBuffers(uint32_t newMaxObjects)
    {
        device.getLogicalDevice().waitIdle();
        
        device.getLogicalDevice().destroyBuffer(objectBuffer);
        device.getLogicalDevice().freeMemory(objectBufferMemory);
        device.getLogicalDevice().destroyBuffer(visibilityBuffer);
        device.getLogicalDevice().freeMemory(visibilityBufferMemory);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);
        
        createBuffers(newMaxObjects);

        loggerInfo("Occlusion culling buffers resized to {} objects", newMaxObjects);
    }

    void OcclusionCullingManager::updateObjects(const std::vector<GPUObjectData>& objects)
    {
        if (objects.empty())
        {
            currentObjectCount = 0;
            return;
        }
        
        if (objects.size() > maxObjectCount)
        {
            resizeBuffers(static_cast<uint32_t>(objects.size() * 2));
        }

        currentObjectCount = static_cast<uint32_t>(objects.size());
        
        vk::Buffer uploadStaging;
        vk::DeviceMemory uploadStagingMemory;
        vk::DeviceSize uploadSize = sizeof(GPUObjectData) * objects.size();

        core::BufferInfoRequest uploadRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            uploadSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::Utilities::createBuffer(uploadRequest, uploadStaging, uploadStagingMemory);
        
        void* data = device.getLogicalDevice().mapMemory(uploadStagingMemory, 0, uploadSize);
        std::memcpy(data, objects.data(), uploadSize);
        device.getLogicalDevice().unmapMemory(uploadStagingMemory);
        
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = device.getStagingCommandPool();
        cmdAllocInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy copyRegion{};
        copyRegion.size = uploadSize;
        cmd.copyBuffer(uploadStaging, objectBuffer, copyRegion);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        device.getLogicalDevice().freeCommandBuffers(device.getStagingCommandPool(), 1, &cmd);
        device.getLogicalDevice().destroyBuffer(uploadStaging);
        device.getLogicalDevice().freeMemory(uploadStagingMemory);
    }

    void OcclusionCullingManager::updateCamera(const glm::mat4& viewProj, float nearPlane)
    {
        cameraData.viewProj = viewProj;
        cameraData.screenSize = glm::vec4(
            static_cast<float>(swapChain.getSwapchainExtent().width),
            static_cast<float>(swapChain.getSwapchainExtent().height),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().width),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().height)
        );
        cameraData.nearPlane = nearPlane;
        cameraData.objectCount = currentObjectCount;
        cameraData.hiZMipLevels = hiZBuffer ? hiZBuffer->getMipLevels() : 1;
        cameraData.padding = 0;
        
        void* data = device.getLogicalDevice().mapMemory(cameraBufferMemory, 0, sizeof(CullCameraData));
        std::memcpy(data, &cameraData, sizeof(CullCameraData));
        device.getLogicalDevice().unmapMemory(cameraBufferMemory);
    }

    void OcclusionCullingManager::cull(vk::CommandBuffer cmd)
    {
        if (!initialized || currentObjectCount == 0 || !hiZBuffer || !hiZBuffer->isInitialized())
        {
            return;
        }
        
        if (needsDescriptorUpdate)
        {
            vk::DescriptorImageInfo hiZInfo{};
            hiZInfo.sampler = hiZBuffer->getHiZSampler();
            hiZInfo.imageView = hiZBuffer->getHiZImageView();
            hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo objectInfo{};
            objectInfo.buffer = objectBuffer;
            objectInfo.offset = 0;
            objectInfo.range = sizeof(GPUObjectData) * maxObjectCount;

            vk::DescriptorBufferInfo visInfo{};
            visInfo.buffer = visibilityBuffer;
            visInfo.offset = 0;
            visInfo.range = sizeof(uint32_t) * maxObjectCount;

            vk::DescriptorBufferInfo camInfo{};
            camInfo.buffer = cameraBuffer;
            camInfo.offset = 0;
            camInfo.range = sizeof(CullCameraData);

            std::array<vk::WriteDescriptorSet, 4> writes{};

            writes[0].dstSet = descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &hiZInfo;

            writes[1].dstSet = descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[1].pBufferInfo = &objectInfo;

            writes[2].dstSet = descriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eStorageBuffer;
            writes[2].pBufferInfo = &visInfo;

            writes[3].dstSet = descriptorSet;
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[3].pBufferInfo = &camInfo;

            device.getLogicalDevice().updateDescriptorSets(writes, {});
            needsDescriptorUpdate = false;
        }
        
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, cullPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        uint32_t groupCount = (currentObjectCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);

        // Barrier for visibility buffer
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = visibilityBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * currentObjectCount;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eVertexShader,
            {}, {}, barrier, {}
        );
    }

    std::vector<uint32_t> OcclusionCullingManager::getVisibilityResults()
    {
        if (currentObjectCount == 0)
        {
            return {};
        }
        
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = device.getStagingCommandPool();
        cmdAllocInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy copyRegion{};
        copyRegion.size = sizeof(uint32_t) * currentObjectCount;
        cmd.copyBuffer(visibilityBuffer, stagingBuffer, copyRegion);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        device.getLogicalDevice().freeCommandBuffers(device.getStagingCommandPool(), 1, &cmd);

        // Read back results
        std::vector<uint32_t> results(currentObjectCount);
        void* data = device.getLogicalDevice().mapMemory(stagingBufferMemory, 0, copyRegion.size);
        std::memcpy(results.data(), data, copyRegion.size);
        device.getLogicalDevice().unmapMemory(stagingBufferMemory);

        return results;
    }

    void OcclusionCullingManager::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyPipeline(cullPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);

        device.getLogicalDevice().destroyBuffer(objectBuffer);
        device.getLogicalDevice().freeMemory(objectBufferMemory);
        device.getLogicalDevice().destroyBuffer(visibilityBuffer);
        device.getLogicalDevice().freeMemory(visibilityBufferMemory);
        device.getLogicalDevice().destroyBuffer(cameraBuffer);
        device.getLogicalDevice().freeMemory(cameraBufferMemory);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }
}
