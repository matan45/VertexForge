#include "ShadowSystem.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include <spdlog/spdlog.h>

namespace render::shadow
{
    ShadowSystem::ShadowSystem(core::Device& device, core::SwapChain& swapChain)
        : device(device)
        , swapChain(swapChain)
    {
    }

    ShadowSystem::~ShadowSystem()
    {
        cleanup();
    }

    void ShadowSystem::init()
    {
        if (initialized)
        {
            spdlog::warn("ShadowSystem::init() called when already initialized");
            return;
        }

        // Initialize atlas manager (for spot lights)
        atlasManager = std::make_unique<ShadowAtlasManager>(device, swapChain);
        atlasManager->init();

        // Initialize resource pool (for CSM arrays and point light cube maps)
        resourcePool = std::make_unique<ShadowResourcePool>(device);
        resourcePool->init();

        // Create shadow data buffer for GPU
        createShadowDataBuffer();
        createDescriptorResources();
        updateDescriptorSet();

        // Reserve space for shadow data
        gpuShadowData.reserve(ShadowConstants::MAX_TOTAL_SHADOW_VIEWS);

        initialized = true;
        spdlog::info("ShadowSystem initialized");
    }

    void ShadowSystem::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        // Cleanup shadow pass pipeline
        if (shadowPassPipeline)
        {
            shadowPassPipeline->cleanup();
            shadowPassPipeline.reset();
        }

        // Cleanup descriptor resources
        if (shadowDataPool)
        {
            logicalDevice.destroyDescriptorPool(shadowDataPool);
            shadowDataPool = nullptr;
        }
        if (shadowDataLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(shadowDataLayout);
            shadowDataLayout = nullptr;
        }

        // Cleanup shadow data buffer
        destroyShadowDataBuffer();

        // Cleanup per-light data
        lightShadowData.clear();
        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        gpuShadowData.clear();

        // Cleanup resource pool
        if (resourcePool)
        {
            resourcePool->cleanup();
            resourcePool.reset();
        }

        // Cleanup atlas manager
        if (atlasManager)
        {
            atlasManager->cleanup();
            atlasManager.reset();
        }

        initialized = false;
        spdlog::info("ShadowSystem cleaned up");
    }

    void ShadowSystem::recreate()
    {
        cleanup();
        init();
    }

    void ShadowSystem::initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                       vk::DescriptorSetLayout meshletDataLayout,
                                       vk::DescriptorSetLayout vertexDataLayout,
                                       vk::DescriptorSetLayout boneMatrixLayout)
    {
        if (!initialized)
        {
            spdlog::error("ShadowSystem::initShadowPass() called before init()");
            return;
        }

        if (shadowPassPipeline)
        {
            spdlog::warn("ShadowSystem::initShadowPass() called when already initialized");
            return;
        }

        shadowPassPipeline = std::make_unique<ShadowPassPipeline>(device, swapChain);
        shadowPassPipeline->init(perDrawLayout, meshletDataLayout, vertexDataLayout, boneMatrixLayout,
                                  atlasManager->getDepthFormat());

        // Create framebuffer for atlas rendering
        shadowPassPipeline->createFramebuffer(
            atlasManager->getAtlasImageView(),
            atlasManager->getAtlasWidth(),
            atlasManager->getAtlasHeight()
        );

        spdlog::info("ShadowSystem: Shadow pass pipeline initialized");
    }

    void ShadowSystem::createShadowDataBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = sizeof(GPUShadowData) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        // Create device-local buffer
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::BufferUtilities::createBuffer(request, shadowDataBuffer, shadowDataMemory);
        }

        // Create staging buffer (host-visible)
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, shadowDataStagingBuffer, shadowDataStagingMemory);
        }

        // Map staging buffer
        shadowDataMapped = logicalDevice.mapMemory(shadowDataStagingMemory, 0, bufferSize);
    }

    void ShadowSystem::destroyShadowDataBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (shadowDataMapped)
        {
            logicalDevice.unmapMemory(shadowDataStagingMemory);
            shadowDataMapped = nullptr;
        }

        if (shadowDataStagingBuffer)
        {
            logicalDevice.destroyBuffer(shadowDataStagingBuffer);
            shadowDataStagingBuffer = nullptr;
        }
        if (shadowDataStagingMemory)
        {
            logicalDevice.freeMemory(shadowDataStagingMemory);
            shadowDataStagingMemory = nullptr;
        }

        if (shadowDataBuffer)
        {
            logicalDevice.destroyBuffer(shadowDataBuffer);
            shadowDataBuffer = nullptr;
        }
        if (shadowDataMemory)
        {
            logicalDevice.freeMemory(shadowDataMemory);
            shadowDataMemory = nullptr;
        }
    }

    void ShadowSystem::createDescriptorResources()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Descriptor set layout for shadow data SSBO
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        shadowDataLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        shadowDataPool = logicalDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = shadowDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadowDataLayout;

        shadowDataDescSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowSystem::updateDescriptorSet()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowDataBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUShadowData) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        vk::WriteDescriptorSet write{};
        write.dstSet = shadowDataDescSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void ShadowSystem::registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings)
    {
        if (lightShadowData.contains(entityId))
        {
            spdlog::warn("ShadowSystem: Light {} already registered, updating settings", entityId);
            updateLightSettings(entityId, settings);
            return;
        }

        LightShadowData data;
        data.settings = settings;
        data.type = type;
        data.lightEntityId = entityId;
        data.matricesDirty = true;
        data.settingsDirty = true;

        // Determine number of views based on type
        uint32_t viewCount = 1;
        switch (type)
        {
            case ShadowMapType::DirectionalCSM:
                viewCount = settings.cascadeCount;
                break;
            case ShadowMapType::PointCube:
                viewCount = 6;  // Cube faces
                break;
            case ShadowMapType::Spot2D:
            case ShadowMapType::Directional2D:
                viewCount = 1;
                break;
            default:
                break;
        }

        data.views.resize(viewCount);

        // Allocate shadow maps in atlas
        if (!allocateShadowMaps(data))
        {
            spdlog::error("ShadowSystem: Failed to allocate shadow maps for light {}", entityId);
            return;
        }

        lightShadowData[entityId] = std::move(data);
        needsUpdate = true;

        spdlog::debug("ShadowSystem: Registered light {} with {} shadow views", entityId, viewCount);
    }

    void ShadowSystem::unregisterLight(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        if (it == lightShadowData.end())
        {
            spdlog::warn("ShadowSystem: Attempted to unregister unknown light {}", entityId);
            return;
        }

        freeShadowMaps(it->second);
        lightShadowData.erase(it);
        needsUpdate = true;

        spdlog::debug("ShadowSystem: Unregistered light {}", entityId);
    }

    void ShadowSystem::updateLightSettings(uint32_t entityId, const ShadowSettings& settings)
    {
        auto* data = getLightShadowData(entityId);
        if (!data)
        {
            spdlog::warn("ShadowSystem: Cannot update settings for unknown light {}", entityId);
            return;
        }

        // Check if resolution changed (requires reallocation)
        bool needsRealloc = data->settings.resolution != settings.resolution;
        bool cascadeCountChanged = (data->type == ShadowMapType::DirectionalCSM &&
                                    data->settings.cascadeCount != settings.cascadeCount);

        data->settings = settings;
        data->settingsDirty = true;
        needsUpdate = true;

        if (needsRealloc || cascadeCountChanged)
        {
            freeShadowMaps(*data);

            if (cascadeCountChanged)
            {
                data->views.resize(settings.cascadeCount);
            }

            if (!allocateShadowMaps(*data))
            {
                spdlog::error("ShadowSystem: Failed to reallocate shadow maps for light {}", entityId);
            }
        }
    }

    bool ShadowSystem::hasLightShadow(uint32_t entityId) const
    {
        return lightShadowData.contains(entityId);
    }

    const LightShadowData* ShadowSystem::getLightShadowData(uint32_t entityId) const
    {
        auto it = lightShadowData.find(entityId);
        return (it != lightShadowData.end()) ? &it->second : nullptr;
    }

    LightShadowData* ShadowSystem::getLightShadowData(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        return (it != lightShadowData.end()) ? &it->second : nullptr;
    }

    bool ShadowSystem::allocateShadowMaps(LightShadowData& data)
    {
        if (!atlasManager || !atlasManager->isInitialized())
            return false;

        uint32_t resolution = data.settings.resolution;

        for (size_t i = 0; i < data.views.size(); ++i)
        {
            auto& view = data.views[i];

            ShadowMapHandle handle = atlasManager->allocate(
                resolution, resolution,
                data.type,
                static_cast<uint32_t>(i)
            );

            if (!handle.isValid())
            {
                // Rollback previous allocations
                for (size_t j = 0; j < i; ++j)
                {
                    atlasManager->free(data.views[j].handle);
                    data.views[j].handle.invalidate();
                }
                return false;
            }

            view.handle = handle;
            view.handle.cascadeIndex = static_cast<uint16_t>(i);
            view.atlasViewport = atlasManager->getNormalizedViewport(handle);
        }

        return true;
    }

    void ShadowSystem::freeShadowMaps(LightShadowData& data)
    {
        if (!atlasManager)
            return;

        for (auto& view : data.views)
        {
            if (view.handle.isValid())
            {
                atlasManager->free(view.handle);
                view.handle.invalidate();
            }
        }
    }

    void ShadowSystem::beginFrame()
    {
        if (!shadowsEnabled)
            return;

        // Clear previous frame's view lists
        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();

        // Collect active shadow views from registered lights
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            for (const auto& view : data.views)
            {
                if (!view.handle.isValid())
                    continue;

                switch (data.type)
                {
                    case ShadowMapType::Directional2D:
                    case ShadowMapType::DirectionalCSM:
                        directionalShadowViews.push_back(view);
                        break;
                    case ShadowMapType::PointCube:
                        pointShadowViews.push_back(view);
                        break;
                    case ShadowMapType::Spot2D:
                        spotShadowViews.push_back(view);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    void ShadowSystem::updateLightShadowMatrices(uint32_t entityId,
                                                  const glm::mat4& cameraView,
                                                  const glm::mat4& cameraProjection,
                                                  float cameraNear,
                                                  float cameraFar)
    {
        auto* data = getLightShadowData(entityId);
        if (!data)
            return;

        // Matrix calculation will be implemented in future subtasks (VK-248, VK-249, VK-250)
        // For now, just mark as updated
        data->matricesDirty = false;
        needsUpdate = true;
    }

    void ShadowSystem::buildGPUShadowData()
    {
        gpuShadowData.clear();

        auto addViews = [this](const std::vector<ShadowView>& views)
        {
            for (const auto& view : views)
            {
                GPUShadowData gpu{};
                gpu.viewProjection = view.viewProjectionMatrix;
                gpu.atlasViewport = view.atlasViewport;
                gpu.biasParams = glm::vec4(
                    view.handle.isValid() ? 0.005f : 0.0f,  // depthBias (placeholder)
                    1.5f,   // slopeBias
                    0.02f,  // normalBias
                    1.0f    // softness
                );
                gpu.rangeParams = glm::vec4(
                    view.nearPlane,
                    view.farPlane,
                    1.0f / (view.farPlane - view.nearPlane),
                    static_cast<float>(view.handle.cascadeIndex)
                );
                gpuShadowData.push_back(gpu);
            }
        };

        addViews(directionalShadowViews);
        addViews(pointShadowViews);
        addViews(spotShadowViews);
    }

    void ShadowSystem::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !shadowsEnabled)
            return;

        // Build GPU data from current views
        buildGPUShadowData();

        if (gpuShadowData.empty())
            return;

        // Copy to staging buffer
        size_t dataSize = sizeof(GPUShadowData) * gpuShadowData.size();
        std::memcpy(shadowDataMapped, gpuShadowData.data(), dataSize);

        // Record copy command
        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;

        cmd.copyBuffer(shadowDataStagingBuffer, shadowDataBuffer, 1, &copyRegion);

        // Memory barrier to ensure copy completes before shader reads
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = shadowDataBuffer;
        barrier.offset = 0;
        barrier.size = dataSize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eVertexShader,
            {},
            0, nullptr,
            1, &barrier,
            0, nullptr
        );

        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd, const ShadowPassParams& params)
    {
        if (!shadowsEnabled || !shadowPassPipeline || !shadowPassPipeline->isInitialized())
            return;

        // Collect all shadow views
        std::vector<const ShadowView*> allViews;
        for (const auto& view : spotShadowViews)
            allViews.push_back(&view);
        for (const auto& view : directionalShadowViews)
            allViews.push_back(&view);
        // Note: point shadow views will use cube maps, handled separately in future

        if (allViews.empty())
            return;

        // 1. Transition atlas from shader read to depth attachment
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = atlasManager->getAtlasImage();
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::PipelineStageFlagBits::eEarlyFragmentTests,
                {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }

        // 2. Begin render pass (entire atlas, clear once)
        vk::ClearValue clearValue{};
        clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = shadowPassPipeline->getRenderPass();
        renderPassInfo.framebuffer = shadowPassPipeline->getFramebuffer();
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{atlasManager->getAtlasWidth(), atlasManager->getAtlasHeight()};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // 3. Bind shadow pipeline
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

        // 4. Bind descriptor sets (same for all shadow views)
        std::array<vk::DescriptorSet, 4> descriptorSets = {
            params.perDrawDataDescSet,   // Set 0: Per-draw data
            params.meshletDataDescSet,   // Set 1: Meshlet data
            params.vertexDataDescSet,    // Set 2: Vertex data
            params.boneMatrixDescSet     // Set 3: Bone matrices
        };
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            shadowPassPipeline->getPipelineLayout(),
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr
        );

        // 5. Render each shadow view
        for (const auto* view : allViews)
        {
            if (!view->handle.isValid())
                continue;

            // Set dynamic viewport and scissor for this tile
            vk::Viewport viewport = atlasManager->getPixelViewport(view->handle);
            cmd.setViewport(0, 1, &viewport);

            vk::Rect2D scissor = atlasManager->getScissorRect(view->handle);
            cmd.setScissor(0, 1, &scissor);

            // Set dynamic depth bias
            // Get bias from light shadow data if available
            float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
            float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
            cmd.setDepthBias(depthBias, 0.0f, slopeBias);

            // Push constants with light view-projection matrix
            ShadowPushConstants pc{};
            pc.lightViewProjection = view->viewProjectionMatrix;
            pc.baseDrawIndex = 0;  // Will iterate through batches
            pc.depthBias = depthBias;
            pc.slopeBias = slopeBias;
            pc.normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

            // Render all batches for this shadow view
            for (uint32_t batch = 0; batch < params.batchCount; ++batch)
            {
                pc.baseDrawIndex = batch * params.commandsPerSection;

                cmd.pushConstants(
                    shadowPassPipeline->getPipelineLayout(),
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                    0,
                    sizeof(ShadowPushConstants),
                    &pc
                );

                // Calculate offset into draw command and count buffers
                vk::DeviceSize commandOffset = batch * params.commandsPerSection * sizeof(vk::DrawMeshTasksIndirectCommandEXT);
                vk::DeviceSize countOffset = batch * sizeof(uint32_t);

                cmd.drawMeshTasksIndirectCountEXT(
                    params.drawCommandBuffer,
                    commandOffset,
                    params.drawCountBuffer,
                    countOffset,
                    params.commandsPerSection,
                    sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                );
            }
        }

        // 6. End render pass
        cmd.endRenderPass();

        // 7. Transition atlas back to shader read for sampling in forward pass
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = atlasManager->getAtlasImage();
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eLateFragmentTests,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }
    }

    vk::DescriptorSetLayout ShadowSystem::getAtlasDescriptorLayout() const
    {
        return atlasManager ? atlasManager->getDescriptorSetLayout() : nullptr;
    }

    vk::DescriptorSet ShadowSystem::getAtlasDescriptorSet() const
    {
        return atlasManager ? atlasManager->getDescriptorSet() : nullptr;
    }

    void ShadowSystem::setGlobalQuality(ShadowQuality quality)
    {
        if (globalQuality == quality)
            return;

        globalQuality = quality;

        // Update all registered lights to new quality
        for (auto& [entityId, data] : lightShadowData)
        {
            data.settings.quality = quality;
            data.settings.resolution = ShadowSettings::getResolutionForQuality(quality);
            data.settingsDirty = true;
        }

        needsUpdate = true;
    }

    uint32_t ShadowSystem::getActiveShadowCasterCount() const
    {
        uint32_t count = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.settings.enabled && data.settings.castShadows)
                ++count;
        }
        return count;
    }

    uint32_t ShadowSystem::getActiveShadowViewCount() const
    {
        return static_cast<uint32_t>(
            directionalShadowViews.size() +
            pointShadowViews.size() +
            spotShadowViews.size()
        );
    }

    float ShadowSystem::getAtlasUtilization() const
    {
        return atlasManager ? atlasManager->getAtlasUtilization() : 0.0f;
    }
}
