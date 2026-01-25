#include "LightOcclusionCulling.hpp"
#include "HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::occlusion
{
    // RAII wrapper for mapped Vulkan memory to ensure exception safety
    class MappedMemoryGuard
    {
    public:
        MappedMemoryGuard(vk::Device device, vk::DeviceMemory memory, vk::DeviceSize offset, vk::DeviceSize size)
            : device(device), memory(memory)
        {
            mappedData = device.mapMemory(memory, offset, size);
        }

        ~MappedMemoryGuard()
        {
            if (mappedData)
            {
                device.unmapMemory(memory);
            }
        }

        MappedMemoryGuard(const MappedMemoryGuard&) = delete;
        MappedMemoryGuard& operator=(const MappedMemoryGuard&) = delete;

        MappedMemoryGuard(MappedMemoryGuard&& other) noexcept
            : device(other.device), memory(other.memory), mappedData(other.mappedData)
        {
            other.mappedData = nullptr;
        }

        MappedMemoryGuard& operator=(MappedMemoryGuard&& other) noexcept
        {
            if (this != &other)
            {
                if (mappedData)
                {
                    device.unmapMemory(memory);
                }
                device = other.device;
                memory = other.memory;
                mappedData = other.mappedData;
                other.mappedData = nullptr;
            }
            return *this;
        }

        void* data() const { return mappedData; }

    private:
        vk::Device device;
        vk::DeviceMemory memory;
        void* mappedData = nullptr;
    };

    LightOcclusionCulling::LightOcclusionCulling(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    LightOcclusionCulling::~LightOcclusionCulling()
    {
        cleanup();
    }

    void LightOcclusionCulling::init(HiZBuffer* hiZ)
    {
        hiZBuffer = hiZ;

        createBuffers(INITIAL_MAX_LIGHTS);
        createComputePipeline();
        createDescriptorSets();

        initialized = true;
        loggerInfo("Light occlusion culling initialized with max {} lights", INITIAL_MAX_LIGHTS);
    }

    void LightOcclusionCulling::createBuffers(uint32_t maxLights)
    {
        maxLightCount = maxLights;

        // Light bounds buffer (GPU only)
        core::BufferInfoRequest boundsRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(GPULightBounds) * maxLights,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(boundsRequest, lightBoundsBuffer, lightBoundsMemory);

        // Visibility buffer (GPU, will be copied to staging for readback)
        core::BufferInfoRequest visRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(uint32_t) * maxLights,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(visRequest, visibilityBuffer, visibilityMemory);

        // Camera buffer (host visible for easy updates)
        core::BufferInfoRequest cameraRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(LightCullCameraData),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(cameraRequest, cameraBuffer, cameraMemory);

        // Staging buffer for visibility readback
        core::BufferInfoRequest stagingRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(uint32_t) * maxLights,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        // Persistent staging buffer for light data upload (avoids per-frame allocation)
        core::BufferInfoRequest uploadStagingRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            sizeof(GPULightBounds) * maxLights,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(uploadStagingRequest, uploadStagingBuffer, uploadStagingMemory);

        // Persistently map the upload staging buffer for efficient CPU writes
        uploadStagingMapped = device.getLogicalDevice().mapMemory(uploadStagingMemory, 0, sizeof(GPULightBounds) * maxLights);

        cachedVisibility.resize(maxLights, 0);
        needsDescriptorUpdate = true;
    }

    void LightOcclusionCulling::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/hiz/light_occlusion_cull.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("Failed to load light occlusion culling shader: {}", shader->getLastCompilationError());
            return;
        }

        // Descriptor set layout: HiZ sampler, light bounds SSBO, visibility SSBO, camera UBO
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

        // Binding 0: Hi-Z pyramid sampler
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 1: Light bounds buffer
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 2: Visibility output buffer
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        // Binding 3: Camera UBO
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

    void LightOcclusionCulling::createDescriptorSets()
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

    void LightOcclusionCulling::resizeBuffers(uint32_t newMaxLights)
    {
        device.getLogicalDevice().waitIdle();

        device.getLogicalDevice().destroyBuffer(lightBoundsBuffer);
        device.getLogicalDevice().freeMemory(lightBoundsMemory);
        device.getLogicalDevice().destroyBuffer(visibilityBuffer);
        device.getLogicalDevice().freeMemory(visibilityMemory);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);

        // Clean up upload staging buffer (unmap before destroy)
        if (uploadStagingMapped)
        {
            device.getLogicalDevice().unmapMemory(uploadStagingMemory);
            uploadStagingMapped = nullptr;
        }
        device.getLogicalDevice().destroyBuffer(uploadStagingBuffer);
        device.getLogicalDevice().freeMemory(uploadStagingMemory);

        createBuffers(newMaxLights);

        loggerInfo("Light occlusion culling buffers resized to {} lights", newMaxLights);
    }

    void LightOcclusionCulling::updateLights(const std::vector<GPULightBounds>& lights)
    {
        resultsCached = false;  // Invalidate cached results
        readbackState = ReadbackState::Idle;  // Reset synchronization state
        visibleLightIds.clear();
        occludedLightIds.clear();

        if (lights.empty())
        {
            currentLightCount = 0;
            lightEntityIds.clear();
            uploadPending = false;
            return;
        }

        if (lights.size() > maxLightCount)
        {
            resizeBuffers(static_cast<uint32_t>(lights.size() * 2));
        }

        currentLightCount = static_cast<uint32_t>(lights.size());

        // Store entity IDs for visibility mapping
        lightEntityIds.resize(lights.size());
        for (size_t i = 0; i < lights.size(); ++i)
        {
            lightEntityIds[i] = lights[i].entityId;
        }

        // Copy light data to persistently mapped staging buffer (CPU-side only, no GPU work)
        pendingUploadSize = sizeof(GPULightBounds) * lights.size();
        std::memcpy(uploadStagingMapped, lights.data(), pendingUploadSize);

        // Mark upload as pending - caller must call recordLightUpload() before cull()
        uploadPending = true;
    }

    bool LightOcclusionCulling::recordLightUpload(vk::CommandBuffer cmd)
    {
        if (!uploadPending || currentLightCount == 0)
        {
            return false;
        }

        // Record copy from staging to GPU buffer
        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = pendingUploadSize;
        cmd.copyBuffer(uploadStagingBuffer, lightBoundsBuffer, copyRegion);

        // Barrier to ensure upload completes before compute shader reads
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = lightBoundsBuffer;
        barrier.offset = 0;
        barrier.size = pendingUploadSize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, barrier, {}
        );

        uploadPending = false;
        return true;
    }

    void LightOcclusionCulling::updateCamera(const glm::mat4& viewProj, const glm::vec3& cameraPos, float nearPlane)
    {
        cameraData.viewProj = viewProj;
        cameraData.screenSize = glm::vec4(
            static_cast<float>(swapChain.getSwapchainExtent().width),
            static_cast<float>(swapChain.getSwapchainExtent().height),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().width),
            1.0f / static_cast<float>(swapChain.getSwapchainExtent().height)
        );
        cameraData.cameraPos = glm::vec4(cameraPos, nearPlane);
        cameraData.lightCount = currentLightCount;
        cameraData.hiZMipLevels = hiZBuffer ? hiZBuffer->getMipLevels() : 1;
        cameraData.padding0 = 0;
        cameraData.padding1 = 0;

        MappedMemoryGuard mapped(device.getLogicalDevice(), cameraMemory, 0, sizeof(LightCullCameraData));
        std::memcpy(mapped.data(), &cameraData, sizeof(LightCullCameraData));
    }

    void LightOcclusionCulling::cull(vk::CommandBuffer cmd)
    {
        if (!initialized || currentLightCount == 0 || !hiZBuffer || !hiZBuffer->isInitialized())
        {
            return;
        }

        resultsCached = false;  // Results will be stale after new cull

        if (needsDescriptorUpdate)
        {
            vk::DescriptorImageInfo hiZInfo{};
            hiZInfo.sampler = hiZBuffer->getHiZSampler();
            hiZInfo.imageView = hiZBuffer->getHiZImageView();
            hiZInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::DescriptorBufferInfo boundsInfo{};
            boundsInfo.buffer = lightBoundsBuffer;
            boundsInfo.offset = 0;
            boundsInfo.range = sizeof(GPULightBounds) * maxLightCount;

            vk::DescriptorBufferInfo visInfo{};
            visInfo.buffer = visibilityBuffer;
            visInfo.offset = 0;
            visInfo.range = sizeof(uint32_t) * maxLightCount;

            vk::DescriptorBufferInfo camInfo{};
            camInfo.buffer = cameraBuffer;
            camInfo.offset = 0;
            camInfo.range = sizeof(LightCullCameraData);

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
            writes[1].pBufferInfo = &boundsInfo;

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

        uint32_t groupCount = (currentLightCount + LIGHT_CULL_WORKGROUP_SIZE - 1) / LIGHT_CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);

        // Barrier for visibility buffer
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = visibilityBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * currentLightCount;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, barrier, {}
        );
    }

    void LightOcclusionCulling::copyResultsToStaging(vk::CommandBuffer cmd)
    {
        if (currentLightCount == 0)
        {
            return;
        }

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(uint32_t) * currentLightCount;
        cmd.copyBuffer(visibilityBuffer, stagingBuffer, copyRegion);

        // Barrier to ensure copy completes before host read
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = stagingBuffer;
        barrier.offset = 0;
        barrier.size = sizeof(uint32_t) * currentLightCount;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            {}, {}, barrier, {}
        );

        // Mark that GPU work is pending - caller must call markResultsReady() after GPU sync
        readbackState = ReadbackState::Pending;
    }

    void LightOcclusionCulling::markResultsReady()
    {
        if (readbackState == ReadbackState::Pending)
        {
            readbackState = ReadbackState::Ready;
        }
    }

    const std::unordered_set<uint32_t>& LightOcclusionCulling::getVisibleLightIds()
    {
        if (resultsCached)
        {
            return visibleLightIds;
        }

        visibleLightIds.clear();
        occludedLightIds.clear();

        if (currentLightCount == 0 || lightEntityIds.empty())
        {
            resultsCached = true;
            readbackState = ReadbackState::Idle;
            return visibleLightIds;
        }

        // Check synchronization state - reading while GPU work is pending is undefined behavior
        if (readbackState == ReadbackState::Pending)
        {
            loggerWarning("LightOcclusionCulling::getVisibleLightIds() called while GPU readback is pending. "
                          "Call markResultsReady() after GPU sync (vkQueueWaitIdle/fence) before reading results. "
                          "Returning empty set to avoid undefined behavior.");
            return visibleLightIds;
        }

        // Read back visibility results from staging buffer
        {
            MappedMemoryGuard mapped(device.getLogicalDevice(), stagingMemory, 0, sizeof(uint32_t) * currentLightCount);
            std::memcpy(cachedVisibility.data(), mapped.data(), sizeof(uint32_t) * currentLightCount);
        }

        // Map visibility results to entity IDs
        for (uint32_t i = 0; i < currentLightCount; ++i)
        {
            uint32_t entityId = lightEntityIds[i];
            if (cachedVisibility[i] != 0)
            {
                visibleLightIds.insert(entityId);
            }
            else
            {
                occludedLightIds.insert(entityId);
            }
        }

        resultsCached = true;
        readbackState = ReadbackState::Idle;
        return visibleLightIds;
    }

    bool LightOcclusionCulling::isLightVisible(uint32_t entityId) const
    {
        return visibleLightIds.contains(entityId);
    }

    void LightOcclusionCulling::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        auto logicalDevice = device.getLogicalDevice();

        logicalDevice.destroyPipeline(cullPipeline);
        logicalDevice.destroyPipelineLayout(pipelineLayout);
        logicalDevice.destroyDescriptorPool(descriptorPool);
        logicalDevice.destroyDescriptorSetLayout(descriptorSetLayout);

        logicalDevice.destroyBuffer(lightBoundsBuffer);
        logicalDevice.freeMemory(lightBoundsMemory);
        logicalDevice.destroyBuffer(visibilityBuffer);
        logicalDevice.freeMemory(visibilityMemory);
        logicalDevice.destroyBuffer(cameraBuffer);
        logicalDevice.freeMemory(cameraMemory);
        logicalDevice.destroyBuffer(stagingBuffer);
        logicalDevice.freeMemory(stagingMemory);

        // Clean up upload staging buffer (unmap before destroy)
        if (uploadStagingMapped)
        {
            logicalDevice.unmapMemory(uploadStagingMemory);
            uploadStagingMapped = nullptr;
        }
        logicalDevice.destroyBuffer(uploadStagingBuffer);
        logicalDevice.freeMemory(uploadStagingMemory);

        shader.reset();
        initialized = false;

        loggerInfo("Light occlusion culling cleaned up");
    }
}
