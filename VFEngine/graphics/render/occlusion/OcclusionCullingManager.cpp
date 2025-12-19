#include "OcclusionCullingManager.hpp"
#include "HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Utilities.hpp"
#include "resource/ShaderResource.hpp"
#include "print/Logger.hpp"
#include <shaderc/shaderc.hpp>
#include <cstring>

namespace render::occlusion {

    constexpr uint32_t INITIAL_MAX_OBJECTS = 1024;
    constexpr uint32_t WORKGROUP_SIZE = 64;

    OcclusionCullingManager::OcclusionCullingManager(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain) {}

    OcclusionCullingManager::~OcclusionCullingManager() {
        cleanup();
    }

    void OcclusionCullingManager::init(HiZBuffer* hiZ) {
        hiZBuffer = hiZ;

        createBuffers(INITIAL_MAX_OBJECTS);
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        loggerInfo("Occlusion culling manager initialized with max {} objects", INITIAL_MAX_OBJECTS);
    }

    void OcclusionCullingManager::createBuffers(uint32_t maxObjects) {
        maxObjectCount = maxObjects;

        // Object buffer (GPU-only for now, will be updated via staging)
        vk::BufferCreateInfo objectBufferInfo{};
        objectBufferInfo.size = sizeof(GPUObjectData) * maxObjects;
        objectBufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        objectBufferInfo.sharingMode = vk::SharingMode::eExclusive;
        objectBuffer = device.getLogicalDevice().createBuffer(objectBufferInfo);

        vk::MemoryRequirements objectMemReqs = device.getLogicalDevice().getBufferMemoryRequirements(objectBuffer);
        vk::MemoryAllocateInfo objectAllocInfo{};
        objectAllocInfo.allocationSize = objectMemReqs.size;
        objectAllocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            objectMemReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        objectBufferMemory = device.getLogicalDevice().allocateMemory(objectAllocInfo);
        device.getLogicalDevice().bindBufferMemory(objectBuffer, objectBufferMemory, 0);

        // Visibility buffer (GPU + CPU readable)
        vk::BufferCreateInfo visBufferInfo{};
        visBufferInfo.size = sizeof(uint32_t) * maxObjects;
        visBufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc;
        visBufferInfo.sharingMode = vk::SharingMode::eExclusive;
        visibilityBuffer = device.getLogicalDevice().createBuffer(visBufferInfo);

        vk::MemoryRequirements visMemReqs = device.getLogicalDevice().getBufferMemoryRequirements(visibilityBuffer);
        vk::MemoryAllocateInfo visAllocInfo{};
        visAllocInfo.allocationSize = visMemReqs.size;
        visAllocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            visMemReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        visibilityBufferMemory = device.getLogicalDevice().allocateMemory(visAllocInfo);
        device.getLogicalDevice().bindBufferMemory(visibilityBuffer, visibilityBufferMemory, 0);

        // Camera uniform buffer (host visible for easy updates)
        vk::BufferCreateInfo cameraBufferInfo{};
        cameraBufferInfo.size = sizeof(CullCameraData);
        cameraBufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        cameraBufferInfo.sharingMode = vk::SharingMode::eExclusive;
        cameraBuffer = device.getLogicalDevice().createBuffer(cameraBufferInfo);

        vk::MemoryRequirements camMemReqs = device.getLogicalDevice().getBufferMemoryRequirements(cameraBuffer);
        vk::MemoryAllocateInfo camAllocInfo{};
        camAllocInfo.allocationSize = camMemReqs.size;
        camAllocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            camMemReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        cameraBufferMemory = device.getLogicalDevice().allocateMemory(camAllocInfo);
        device.getLogicalDevice().bindBufferMemory(cameraBuffer, cameraBufferMemory, 0);

        // Staging buffer for readback
        vk::BufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.size = sizeof(uint32_t) * maxObjects;
        stagingBufferInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
        stagingBufferInfo.sharingMode = vk::SharingMode::eExclusive;
        stagingBuffer = device.getLogicalDevice().createBuffer(stagingBufferInfo);

        vk::MemoryRequirements stagingMemReqs = device.getLogicalDevice().getBufferMemoryRequirements(stagingBuffer);
        vk::MemoryAllocateInfo stagingAllocInfo{};
        stagingAllocInfo.allocationSize = stagingMemReqs.size;
        stagingAllocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            stagingMemReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        stagingBufferMemory = device.getLogicalDevice().allocateMemory(stagingAllocInfo);
        device.getLogicalDevice().bindBufferMemory(stagingBuffer, stagingBufferMemory, 0);

        needsDescriptorUpdate = true;
    }

    void OcclusionCullingManager::createComputePipeline() {
        // Load and compile compute shader
        auto shaders = resource::ShaderResource::readShaderFile("../../resources/shaders/hiz/hiz_cull.glsl");
        if (shaders.empty()) {
            loggerError("Failed to load occlusion culling shader source");
            return;
        }

        std::string computeSource;
        for (const auto& shader : shaders) {
            if (shader.type == resource::ShaderType::COMPUTE) {
                computeSource = shader.source;
                break;
            }
        }

        if (computeSource.empty()) {
            loggerError("No compute shader found in hiz_cull.glsl");
            return;
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);

        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            computeSource.data(), computeSource.size(),
            shaderc_compute_shader, "hiz_cull.glsl", options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            loggerError("Occlusion culling shader compilation failed: {}", result.GetErrorMessage());
            return;
        }

        std::vector<uint32_t> spirvCode(result.cbegin(), result.cend());

        vk::ShaderModuleCreateInfo shaderInfo{};
        shaderInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        shaderInfo.pCode = spirvCode.data();
        vk::ShaderModule shaderModule = device.getLogicalDevice().createShaderModule(shaderInfo);

        // Descriptor set layout
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Hi-Z sampler
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Object buffer
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Visibility buffer
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Camera UBO
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // Pipeline layout
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Compute pipeline
        vk::PipelineShaderStageCreateInfo stageInfo{};
        stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
        stageInfo.module = shaderModule;
        stageInfo.pName = "main";

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = pipelineLayout;

        auto pipelineResult = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        cullPipeline = pipelineResult.value;

        device.getLogicalDevice().destroyShaderModule(shaderModule);
    }

    void OcclusionCullingManager::createDescriptorSets() {
        // Create descriptor pool
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

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        needsDescriptorUpdate = true;
    }

    void OcclusionCullingManager::resizeBuffers(uint32_t newMaxObjects) {
        device.getLogicalDevice().waitIdle();

        // Destroy old buffers
        device.getLogicalDevice().destroyBuffer(objectBuffer);
        device.getLogicalDevice().freeMemory(objectBufferMemory);
        device.getLogicalDevice().destroyBuffer(visibilityBuffer);
        device.getLogicalDevice().freeMemory(visibilityBufferMemory);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingBufferMemory);

        // Create new buffers
        createBuffers(newMaxObjects);

        loggerInfo("Occlusion culling buffers resized to {} objects", newMaxObjects);
    }

    void OcclusionCullingManager::updateObjects(const std::vector<GPUObjectData>& objects) {
        if (objects.empty()) {
            currentObjectCount = 0;
            return;
        }

        // Resize if needed
        if (objects.size() > maxObjectCount) {
            resizeBuffers(static_cast<uint32_t>(objects.size() * 2));
        }

        currentObjectCount = static_cast<uint32_t>(objects.size());

        // Create staging buffer for upload
        vk::BufferCreateInfo stagingInfo{};
        stagingInfo.size = sizeof(GPUObjectData) * objects.size();
        stagingInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingInfo.sharingMode = vk::SharingMode::eExclusive;
        vk::Buffer uploadStaging = device.getLogicalDevice().createBuffer(stagingInfo);

        vk::MemoryRequirements memReqs = device.getLogicalDevice().getBufferMemoryRequirements(uploadStaging);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(
            device.getPhysicalDevice(),
            memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        vk::DeviceMemory uploadStagingMemory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindBufferMemory(uploadStaging, uploadStagingMemory, 0);

        // Copy data to staging
        void* data = device.getLogicalDevice().mapMemory(uploadStagingMemory, 0, stagingInfo.size);
        std::memcpy(data, objects.data(), stagingInfo.size);
        device.getLogicalDevice().unmapMemory(uploadStagingMemory);

        // Copy to device buffer
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = device.getStagingCommandPool();
        cmdAllocInfo.commandBufferCount = 1;
        vk::CommandBuffer cmd = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo)[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::BufferCopy copyRegion{};
        copyRegion.size = stagingInfo.size;
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

    void OcclusionCullingManager::updateCamera(const glm::mat4& viewProj, float nearPlane) {
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

        // Update camera buffer
        void* data = device.getLogicalDevice().mapMemory(cameraBufferMemory, 0, sizeof(CullCameraData));
        std::memcpy(data, &cameraData, sizeof(CullCameraData));
        device.getLogicalDevice().unmapMemory(cameraBufferMemory);
    }

    void OcclusionCullingManager::cull(vk::CommandBuffer cmd) {
        if (!initialized || currentObjectCount == 0 || !hiZBuffer || !hiZBuffer->isInitialized()) {
            return;
        }

        // Update descriptor set if needed
        if (needsDescriptorUpdate) {
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

        // Bind pipeline and dispatch
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

    std::vector<uint32_t> OcclusionCullingManager::getVisibilityResults() {
        if (currentObjectCount == 0) {
            return {};
        }

        // Copy visibility buffer to staging
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

    void OcclusionCullingManager::cleanup() {
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

        initialized = false;
    }

}
