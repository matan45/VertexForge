#include "ShadowSystem.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <spdlog/spdlog.h>
#include <chrono>

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

        // Create shadow texture descriptor for forward pass
        createShadowTextureDescriptor();

        // Transition atlas image to shader-read-optimal for initial binding
        // (will be transitioned to depth attachment during shadow pass if needed)
        {
            const auto& logicalDevice = device.getLogicalDevice();
            auto cmd = core::Utilities::beginSingleTimeCommands(logicalDevice, device.getStagingCommandPool());

            vk::ImageMemoryBarrier barrier{};
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = atlasManager->getAtlasImage();
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            cmd->pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd, nullptr);

            // Mark atlas as no longer in undefined state so shadow pass uses correct transition
            atlasFirstUse = false;
        }

        // Initialize with placeholder bindings to avoid validation errors before first uploadToGPU
        updateShadowTextureDescriptor();

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

        // Cleanup pending cube framebuffers
        for (vk::Framebuffer fb : pendingCubeFramebuffers)
        {
            logicalDevice.destroyFramebuffer(fb);
        }
        pendingCubeFramebuffers.clear();

        // Cleanup shadow pass pipeline
        if (shadowPassPipeline)
        {
            shadowPassPipeline->cleanup();
            shadowPassPipeline.reset();
        }

        // Cleanup descriptor resources
        destroyShadowTextureDescriptor();

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
        atlasFirstUse = true;  // Reset so next use transitions from eUndefined
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

    void ShadowSystem::createShadowTextureDescriptor()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Create descriptor set layout for shadow textures:
        // Binding 0: Spot light atlas (sampler2DShadow)
        // Binding 1: CSM cascades array (sampler2DArrayShadow)
        // Binding 2: Point light cube maps array (samplerCubeShadow[])
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Binding 0: Spot atlas with comparison sampler
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 1: CSM cascade array with comparison sampler
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 2: Point light cube maps (array of cube samplers)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = ShadowConstants::MAX_POINT_SHADOW_CASTERS;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        // Use partially bound flag for the cube map array since not all slots may be used
        std::array<vk::DescriptorBindingFlags, 3> bindingFlags{};
        bindingFlags[0] = {};
        bindingFlags[1] = {};
        bindingFlags[2] = vk::DescriptorBindingFlagBits::ePartiallyBound;

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();
        layoutInfo.pNext = &bindingFlagsInfo;

        shadowTextureLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        // Create descriptor pool
        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2 + ShadowConstants::MAX_POINT_SHADOW_CASTERS;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        shadowTexturePool = logicalDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = shadowTexturePool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadowTextureLayout;

        shadowTextureDescSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];

        // Initial update with placeholder/null bindings will happen in updateShadowTextureDescriptor
    }

    void ShadowSystem::updateShadowTextureDescriptor()
    {
        if (!atlasManager || !resourcePool)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        std::vector<vk::WriteDescriptorSet> writes;

        // We need separate vectors for image infos to ensure stable pointers
        std::vector<vk::DescriptorImageInfo> atlasInfos(1);
        std::vector<vk::DescriptorImageInfo> csmInfos(1);
        std::vector<vk::DescriptorImageInfo> cubeInfos;
        cubeInfos.reserve(ShadowConstants::MAX_POINT_SHADOW_CASTERS);

        // Binding 0: Spot light atlas (sampler2DShadow)
        atlasInfos[0].sampler = atlasManager->getComparisonSampler();
        atlasInfos[0].imageView = atlasManager->getAtlasImageView();
        atlasInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet atlasWrite{};
        atlasWrite.dstSet = shadowTextureDescSet;
        atlasWrite.dstBinding = 0;
        atlasWrite.dstArrayElement = 0;
        atlasWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        atlasWrite.descriptorCount = 1;
        atlasWrite.pImageInfo = atlasInfos.data();
        writes.push_back(atlasWrite);

        // Binding 1: CSM cascade array (sampler2DArrayShadow)
        // Find first active CSM light and bind its texture array
        // Use placeholder array view (proper 2D array type) as fallback instead of 2D atlas
        vk::ImageView csmArrayView = resourcePool->getPlaceholderArrayView();
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::DirectionalCSM &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                ShadowDepthArray* array = resourcePool->getArray(data.resourceHandle);
                if (array && array->isInitialized())
                {
                    csmArrayView = array->getArrayView();
                    break;  // Use first valid CSM array
                }
            }
        }

        csmInfos[0].sampler = resourcePool->getComparisonSampler();
        csmInfos[0].imageView = csmArrayView;
        csmInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet csmWrite{};
        csmWrite.dstSet = shadowTextureDescSet;
        csmWrite.dstBinding = 1;
        csmWrite.dstArrayElement = 0;
        csmWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        csmWrite.descriptorCount = 1;
        csmWrite.pImageInfo = csmInfos.data();
        writes.push_back(csmWrite);

        // Binding 2: Point light cube maps (samplerCubeShadow[])
        // Bind cube maps for each point light in order of their shadow index
        // The shadow index for a point light corresponds to its position in pointShadowViews,
        // which determines its slot in the cube array binding
        uint32_t cubeIndex = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;

            ShadowCubeMap* cube = resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            if (cubeIndex >= ShadowConstants::MAX_POINT_SHADOW_CASTERS)
            {
                spdlog::warn("ShadowSystem: Exceeded max point shadow casters ({}), skipping light {}",
                             ShadowConstants::MAX_POINT_SHADOW_CASTERS, entityId);
                break;
            }

            vk::DescriptorImageInfo cubeInfo{};
            cubeInfo.sampler = resourcePool->getCubeComparisonSampler();
            cubeInfo.imageView = cube->getCubeView();
            cubeInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cubeInfos.push_back(cubeInfo);
            ++cubeIndex;
        }

        // Always bind at least a placeholder cube to keep descriptor valid
        // If no point lights have shadows, bind the placeholder
        if (cubeInfos.empty())
        {
            vk::ImageView placeholderCubeView = resourcePool->getPlaceholderCubeView();
            if (placeholderCubeView)
            {
                vk::DescriptorImageInfo placeholderInfo{};
                placeholderInfo.sampler = resourcePool->getCubeComparisonSampler();
                placeholderInfo.imageView = placeholderCubeView;
                placeholderInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cubeInfos.push_back(placeholderInfo);
            }
        }

        // Write cube descriptors (either real cubes or placeholder)
        if (!cubeInfos.empty())
        {
            vk::WriteDescriptorSet cubeWrite{};
            cubeWrite.dstSet = shadowTextureDescSet;
            cubeWrite.dstBinding = 2;
            cubeWrite.dstArrayElement = 0;
            cubeWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            cubeWrite.descriptorCount = static_cast<uint32_t>(cubeInfos.size());
            cubeWrite.pImageInfo = cubeInfos.data();
            writes.push_back(cubeWrite);
        }

        if (!writes.empty())
        {
            logicalDevice.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void ShadowSystem::destroyShadowTextureDescriptor()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (shadowTexturePool)
        {
            logicalDevice.destroyDescriptorPool(shadowTexturePool);
            shadowTexturePool = nullptr;
        }
        if (shadowTextureLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(shadowTextureLayout);
            shadowTextureLayout = nullptr;
        }
    }

    bool ShadowSystem::registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings)
    {
        if (lightShadowData.contains(entityId))
        {
            spdlog::warn("ShadowSystem: Light {} already registered, updating settings", entityId);
            updateLightSettings(entityId, settings);
            return true;  // Already registered is considered success
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
            return false;
        }

        lightShadowData[entityId] = std::move(data);
        needsUpdate = true;

        spdlog::debug("ShadowSystem: Registered light {} with {} shadow views", entityId, viewCount);
        return true;
    }

    void ShadowSystem::unregisterLight(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        if (it == lightShadowData.end())
        {
            spdlog::warn("ShadowSystem: Attempted to unregister unknown light {}", entityId);
            return;
        }

        // Wait for GPU to finish using resources before destroying them
        // This is necessary because freeShadowMaps destroys Vulkan objects immediately
        // and the GPU might still be using them from the previous frame.
        // TODO: Replace with deferred deletion queue for better performance
        device.getLogicalDevice().waitIdle();

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

    int32_t ShadowSystem::getShadowViewIndex(uint32_t entityId) const
    {
        if (!shadowsEnabled)
            return -1;

        auto it = entityToShadowIndex.find(entityId);
        if (it != entityToShadowIndex.end())
            return it->second;

        return -1;
    }

    bool ShadowSystem::allocateShadowMaps(LightShadowData& data)
    {
        uint32_t resolution = data.settings.resolution;

        switch (data.type)
        {
            case ShadowMapType::Spot2D:
            case ShadowMapType::Directional2D:
            {
                // Use atlas for 2D shadow maps
                if (!atlasManager || !atlasManager->isInitialized())
                    return false;

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

            case ShadowMapType::DirectionalCSM:
            {
                // Use resource pool for CSM texture array
                if (!resourcePool || !resourcePool->isInitialized())
                    return false;

                uint32_t cascadeCount = static_cast<uint32_t>(data.views.size());
                ShadowResourceHandle handle = resourcePool->allocateArray(resolution, resolution, cascadeCount);

                if (!handle.isValid())
                {
                    spdlog::error("ShadowSystem: Failed to allocate CSM array {}x{} with {} cascades",
                                  resolution, resolution, cascadeCount);
                    return false;
                }

                data.resourceHandle = handle;

                // Set up view metadata (atlas viewport not used for CSM, repurpose for cascade info)
                for (size_t i = 0; i < data.views.size(); ++i)
                {
                    auto& view = data.views[i];
                    view.handle.type = ShadowMapType::DirectionalCSM;
                    view.handle.cascadeIndex = static_cast<uint16_t>(i);
                    // For CSM: atlasViewport.z = cascadeCount, atlasViewport.w = layer/cascade index
                    view.atlasViewport = glm::vec4(0.0f, 0.0f, static_cast<float>(cascadeCount), static_cast<float>(i));
                }

                spdlog::debug("ShadowSystem: Allocated CSM array {}x{} with {} cascades",
                              resolution, resolution, cascadeCount);
                return true;
            }

            case ShadowMapType::PointCube:
            {
                // Use resource pool for cube map
                if (!resourcePool || !resourcePool->isInitialized())
                    return false;

                ShadowResourceHandle handle = resourcePool->allocateCube(resolution);

                if (!handle.isValid())
                {
                    spdlog::error("ShadowSystem: Failed to allocate point cube map {}x{}", resolution, resolution);
                    return false;
                }

                data.resourceHandle = handle;

                // Set up view metadata for each cube face
                for (size_t i = 0; i < data.views.size(); ++i)
                {
                    auto& view = data.views[i];
                    view.handle.type = ShadowMapType::PointCube;
                    view.handle.layer = static_cast<uint32_t>(i);  // Cube face index
                    // For cubes, atlasViewport.w stores the face index
                    view.atlasViewport = glm::vec4(0.0f, 0.0f, 1.0f, static_cast<float>(i));
                }

                spdlog::debug("ShadowSystem: Allocated point cube map {}x{}", resolution, resolution);
                return true;
            }

            default:
                return false;
        }
    }

    void ShadowSystem::freeShadowMaps(LightShadowData& data)
    {
        // Free dedicated resource (CSM array or cube map)
        if (data.resourceHandle.isValid() && resourcePool)
        {
            resourcePool->free(data.resourceHandle);
            data.resourceHandle.invalidate();
        }

        // Free atlas tiles (for Spot2D and Directional2D)
        if (atlasManager)
        {
            for (auto& view : data.views)
            {
                if (view.handle.isValid() && data.usesAtlas())
                {
                    atlasManager->free(view.handle);
                    view.handle.invalidate();
                }
            }
        }
    }

    void ShadowSystem::beginFrame(const std::unordered_set<uint32_t>* visibleLightIds)
    {
        if (!shadowsEnabled)
            return;

        // Clear previous frame's data
        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();

        // Update shadow matrices for all registered lights
        // For point/spot lights, this extracts position from WorldTransformComponent
        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            // Point lights: update cube face matrices from current world transform
            if (data.type == ShadowMapType::PointCube)
            {
                auto entity = static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<components::WorldTransformComponent>(entity))
                    continue;

                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                glm::vec3 lightPosition = glm::vec3(worldTransform.worldMatrix[3]);

                // Get radius from PointLightComponent (used as far plane)
                float farPlane = data.settings.farPlane;
                if (registry.all_of<components::PointLightComponent>(entity))
                {
                    const auto& pointLight = registry.get<components::PointLightComponent>(entity);
                    farPlane = pointLight.radius;
                }
                float nearPlane = data.settings.nearPlane;

                // Compute all 6 cube face matrices
                auto faceMatrices = PointShadowCalculator::computeCubeFaceMatrices(
                    lightPosition, nearPlane, farPlane);

                // Update each face view
                for (uint32_t face = 0; face < PointShadowCalculator::FACE_COUNT && face < data.views.size(); ++face)
                {
                    auto& view = data.views[face];
                    const auto& faceData = faceMatrices[face];

                    view.viewMatrix = faceData.viewMatrix;
                    view.projectionMatrix = faceData.projMatrix;
                    view.viewProjectionMatrix = faceData.viewProjMatrix;
                    view.nearPlane = nearPlane;
                    view.farPlane = farPlane;
                    view.lightPosition = glm::vec4(lightPosition, 1.0f);
                    view.handle.layer = face;
                }
            }
        }

        // Temporary maps to track per-type indices for each entity
        std::unordered_map<uint32_t, int32_t> directionalIndices;
        std::unordered_map<uint32_t, int32_t> pointIndices;
        std::unordered_map<uint32_t, int32_t> spotIndices;

        // Collect active shadow views from registered lights
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            // Skip culled lights (but always include directional lights - they're global)
            bool isDirectional = (data.type == ShadowMapType::DirectionalCSM ||
                                  data.type == ShadowMapType::Directional2D);
            if (visibleLightIds && !isDirectional && !visibleLightIds->contains(entityId))
                continue;

            switch (data.type)
            {
                case ShadowMapType::Directional2D:
                {
                    // Atlas-based: each view has its own atlas tile
                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);
                    for (const auto& view : data.views)
                    {
                        if (!view.handle.isValid())
                            continue;

                        ShadowView viewCopy = view;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.pcfKernelRadius = globalPcfKernel;
                        viewCopy.pcfSoftness = data.settings.softness;
                        viewCopy.filterEnabled = globalSoftShadowsEnabled;

                        if (!directionalIndices.contains(entityId))
                            directionalIndices[entityId] = static_cast<int32_t>(directionalShadowViews.size());
                        directionalShadowViews.push_back(viewCopy);
                    }
                    break;
                }

                case ShadowMapType::DirectionalCSM:
                {
                    // CSM: uses dedicated texture array, all cascades share one resource handle
                    if (!data.resourceHandle.isValid())
                        continue;

                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);

                    // Add all cascade views (each becomes an entry in shadow data buffer)
                    for (size_t i = 0; i < data.views.size(); ++i)
                    {
                        const auto& view = data.views[i];
                        ShadowView viewCopy = view;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.pcfKernelRadius = globalPcfKernel;
                        viewCopy.pcfSoftness = data.settings.softness;
                        viewCopy.filterEnabled = globalSoftShadowsEnabled;

                        if (i == 0)
                            directionalIndices[entityId] = static_cast<int32_t>(directionalShadowViews.size());
                        directionalShadowViews.push_back(viewCopy);
                    }
                    break;
                }

                case ShadowMapType::Spot2D:
                {
                    // Atlas-based: single view per spot light
                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);
                    for (const auto& view : data.views)
                    {
                        if (!view.handle.isValid())
                            continue;

                        ShadowView viewCopy = view;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.pcfKernelRadius = globalPcfKernel;
                        viewCopy.pcfSoftness = data.settings.softness;
                        viewCopy.filterEnabled = globalSoftShadowsEnabled;

                        if (!spotIndices.contains(entityId))
                            spotIndices[entityId] = static_cast<int32_t>(spotShadowViews.size());
                        spotShadowViews.push_back(viewCopy);
                    }
                    break;
                }

                case ShadowMapType::PointCube:
                {
                    // Point cube: uses dedicated cube map, add ONE entry per light
                    // The shader samples using direction vector, so we only need one shadow data entry
                    // containing near/far/bias params (cube faces are implicit in direction sampling)
                    if (!data.resourceHandle.isValid() || data.views.empty())
                        continue;

                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);

                    // Use first view's data (all faces share same near/far/bias)
                    const auto& view = data.views[0];
                    ShadowView viewCopy = view;
                    viewCopy.depthBias = data.settings.depthBias;
                    viewCopy.slopeBias = data.settings.slopeBias;
                    viewCopy.normalBias = data.settings.normalBias;
                    viewCopy.texelSize = texelSize;
                    viewCopy.pcfKernelRadius = globalPcfKernel;
                    viewCopy.pcfSoftness = data.settings.softness;
                    viewCopy.filterEnabled = globalSoftShadowsEnabled;
                    viewCopy.entityId = entityId;  // Set entity ID for cube map index lookup

                    pointIndices[entityId] = static_cast<int32_t>(pointShadowViews.size());
                    pointShadowViews.push_back(viewCopy);
                    break;
                }

                default:
                    break;
            }
        }

        // Compute final GPU shadow data indices
        // Layout: [directional views] [point views] [spot views]
        const int32_t directionalOffset = 0;
        const int32_t pointOffset = static_cast<int32_t>(directionalShadowViews.size());
        const int32_t spotOffset = pointOffset + static_cast<int32_t>(pointShadowViews.size());

        for (const auto& [entityId, localIdx] : directionalIndices)
            entityToShadowIndex[entityId] = directionalOffset + localIdx;
        for (const auto& [entityId, localIdx] : pointIndices)
            entityToShadowIndex[entityId] = pointOffset + localIdx;
        for (const auto& [entityId, localIdx] : spotIndices)
            entityToShadowIndex[entityId] = spotOffset + localIdx;
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

        // Get entity registry and validate entity
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);

        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
        {
            spdlog::warn("ShadowSystem: Entity {} missing WorldTransformComponent", entityId);
            data->matricesDirty = false;
            return;
        }

        const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

        // Handle PointCube shadow type (VK-249)
        if (data->type == ShadowMapType::PointCube)
        {
            // Extract light position from world transform matrix
            glm::vec3 lightPosition = glm::vec3(worldTransform.worldMatrix[3]);

            // Get radius from PointLightComponent (used as far plane)
            float farPlane = data->settings.farPlane;
            if (registry.all_of<components::PointLightComponent>(entity))
            {
                const auto& pointLight = registry.get<components::PointLightComponent>(entity);
                farPlane = pointLight.radius;
            }

            float nearPlane = data->settings.nearPlane;

            // Compute all 6 cube face matrices
            auto faceMatrices = PointShadowCalculator::computeCubeFaceMatrices(
                lightPosition,
                nearPlane,
                farPlane
            );

            // Ensure we have 6 views allocated
            if (data->views.size() != PointShadowCalculator::FACE_COUNT)
            {
                spdlog::error("ShadowSystem: PointCube light {} has {} views, expected 6",
                              entityId, data->views.size());
                data->matricesDirty = false;
                return;
            }

            // Update each face view
            for (uint32_t face = 0; face < PointShadowCalculator::FACE_COUNT; ++face)
            {
                auto& view = data->views[face];
                const auto& faceData = faceMatrices[face];

                view.viewMatrix = faceData.viewMatrix;
                view.projectionMatrix = faceData.projMatrix;
                view.viewProjectionMatrix = faceData.viewProjMatrix;
                view.nearPlane = nearPlane;
                view.farPlane = farPlane;
                view.lightPosition = glm::vec4(lightPosition, 1.0f);

                // Copy bias settings from per-light settings
                view.depthBias = data->settings.depthBias;
                view.slopeBias = data->settings.slopeBias;
                view.normalBias = data->settings.normalBias;

                // Face index stored in handle for GPU access
                view.handle.layer = face;
            }

            data->matricesDirty = false;
            needsUpdate = true;
            return;
        }

        // Handle Spot2D shadow type (VK-250)
        if (data->type == ShadowMapType::Spot2D)
        {
            // Extract light position from world transform matrix
            glm::vec3 lightPosition = glm::vec3(worldTransform.worldMatrix[3]);

            // Extract light direction from world transform matrix
            // Spot lights point along negative Z axis in local space
            glm::vec3 lightDirection = glm::normalize(
                glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
            );

            // Get spot light parameters
            float outerAngle = 45.0f;  // Default outer cone angle
            float range = 20.0f;       // Default range
            if (registry.all_of<components::SpotLightComponent>(entity))
            {
                const auto& spotLight = registry.get<components::SpotLightComponent>(entity);
                outerAngle = spotLight.outerAngle;
                range = spotLight.range;
            }

            float nearPlane = data->settings.nearPlane;

            // Compute spot light shadow matrices
            auto shadowData = SpotShadowCalculator::computeSpotLightMatrices(
                lightPosition,
                lightDirection,
                outerAngle,
                nearPlane,
                range
            );

            // Ensure we have at least 1 view allocated
            if (data->views.empty())
            {
                spdlog::error("ShadowSystem: Spot2D light {} has no views allocated", entityId);
                data->matricesDirty = false;
                return;
            }

            // Update the single view
            auto& view = data->views[0];
            view.viewMatrix = shadowData.viewMatrix;
            view.projectionMatrix = shadowData.projMatrix;
            view.viewProjectionMatrix = shadowData.viewProjMatrix;
            view.nearPlane = nearPlane;
            view.farPlane = range;
            view.lightPosition = glm::vec4(lightPosition, 1.0f);
            view.lightDirection = glm::vec4(lightDirection, 0.0f);

            // Copy bias settings from per-light settings
            view.depthBias = data->settings.depthBias;
            view.slopeBias = data->settings.slopeBias;
            view.normalBias = data->settings.normalBias;

            data->matricesDirty = false;
            needsUpdate = true;
            return;
        }

        // Handle DirectionalCSM (VK-248)
        if (data->type != ShadowMapType::DirectionalCSM)
        {
            // Unknown shadow type - mark as processed
            data->matricesDirty = false;
            needsUpdate = true;
            return;
        }

        // DirectionalCSM implementation continues below
        // (worldTransform already validated and retrieved above)

        // Extract light direction from world transform matrix
        // Light points along negative Z axis in local space (0, 0, -1)
        glm::vec3 lightDirection = glm::normalize(
            glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
        );

        // Get cascade split mode from settings
        // TODO: When RenderSettings integration is complete, get from there
        // For now, use Practical mode with the lambda from per-light settings
        types::CascadeSplitMode splitMode = types::CascadeSplitMode::Practical;

        // Compute cascade split distances
        auto splits = CascadeShadowCalculator::computeSplitDistances(
            cameraNear,
            cameraFar,
            data->settings.cascadeCount,
            splitMode,
            data->settings.cascadeSplitLambda
        );

        // Update each cascade view
        uint32_t viewCount = std::min(static_cast<uint32_t>(data->views.size()),
                                       data->settings.cascadeCount);

        // Warn if view count doesn't match expected cascade count (indicates configuration issue)
        if (data->views.size() != data->settings.cascadeCount)
        {
            spdlog::warn("ShadowSystem: DirectionalCSM light {} has {} views but cascadeCount is {}",
                         entityId, data->views.size(), data->settings.cascadeCount);
        }

        for (uint32_t i = 0; i < viewCount; ++i)
        {
            auto& view = data->views[i];

            float cascadeNear = splits[i];
            float cascadeFar = splits[i + 1];

            // Get frustum corners in world space for this cascade range
            auto frustumCorners = CascadeShadowCalculator::getFrustumCornersWorldSpace(
                cameraView,
                cameraProjection,
                cascadeNear,
                cascadeFar
            );

            // Compute stable cascade matrix with texel snapping
            auto cascadeData = CascadeShadowCalculator::computeCascadeMatrix(
                frustumCorners,
                lightDirection,
                data->settings.resolution
            );

            // Update view data
            view.viewMatrix = cascadeData.viewMatrix;
            view.projectionMatrix = cascadeData.projMatrix;
            view.viewProjectionMatrix = cascadeData.viewProjMatrix;
            view.nearPlane = cascadeData.nearDistance;
            view.farPlane = cascadeData.farDistance;
            view.lightDirection = glm::vec4(lightDirection, 0.0f);

            // Copy bias settings from per-light settings
            view.depthBias = data->settings.depthBias;
            view.slopeBias = data->settings.slopeBias;
            view.normalBias = data->settings.normalBias;

            // Cascade index is stored in handle for GPU access
            view.handle.cascadeIndex = static_cast<uint16_t>(i);
        }

        data->matricesDirty = false;
        needsUpdate = true;
    }

    void ShadowSystem::buildGPUShadowData()
    {
        gpuShadowData.clear();

        // Build entity to cube map index mapping
        // This matches the order in updateShadowTextureDescriptor
        std::unordered_map<uint32_t, int32_t> entityToCubeIndex;
        int32_t cubeIdx = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::PointCube &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                ShadowCubeMap* cube = resourcePool ? resourcePool->getCube(data.resourceHandle) : nullptr;
                if (cube && cube->isInitialized())
                {
                    entityToCubeIndex[entityId] = cubeIdx++;
                }
            }
        }

        auto addViews = [&](const std::vector<ShadowView>& views, bool isPointLight)
        {
            for (const auto& view : views)
            {
                GPUShadowData gpu{};
                gpu.viewProjection = view.viewProjectionMatrix;
                gpu.atlasViewport = view.atlasViewport;

                // Use bias values from the view (set by updateLightShadowMatrices)
                // biasParams.w stores texelSize (1.0/resolution) for PCF filtering
                gpu.biasParams = glm::vec4(
                    view.depthBias,
                    view.slopeBias,
                    view.normalBias,
                    view.texelSize
                );

                // Range params: near, far, inverse range, cascade index
                float range = view.farPlane - view.nearPlane;
                float invRange = (range > 0.0001f) ? (1.0f / range) : 0.0f;
                gpu.rangeParams = glm::vec4(
                    view.nearPlane,
                    view.farPlane,
                    invRange,
                    static_cast<float>(view.handle.cascadeIndex)
                );

                // PCF params: kernel radius, softness, filter enabled, cubeMapIndex
                // cubeMapIndex is -1 for non-point lights, otherwise the index into shadowCubes[]
                float cubeMapIndex = -1.0f;
                if (isPointLight)
                {
                    auto it = entityToCubeIndex.find(view.entityId);
                    if (it != entityToCubeIndex.end())
                    {
                        cubeMapIndex = static_cast<float>(it->second);
                    }
                }

                gpu.pcfParams = glm::vec4(
                    static_cast<float>(view.pcfKernelRadius),
                    view.pcfSoftness,
                    view.filterEnabled ? 1.0f : 0.0f,
                    cubeMapIndex
                );

                gpuShadowData.push_back(gpu);
            }
        };

        addViews(directionalShadowViews, false);
        addViews(pointShadowViews, true);
        addViews(spotShadowViews, false);
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

        // Update shadow texture descriptor with current shadow resources
        updateShadowTextureDescriptor();

        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd, const ShadowPassParams& params)
    {
        if (!shadowsEnabled || !shadowPassPipeline || !shadowPassPipeline->isInitialized())
            return;

        // Collect all shadow views (spot and directional - these use the atlas)
        std::vector<const ShadowView*> allViews;
        for (const auto& view : spotShadowViews)
            allViews.push_back(&view);
        for (const auto& view : directionalShadowViews)
            allViews.push_back(&view);

        // Check if we have ANY shadow work to do (atlas views OR point cube shadows)
        bool hasAtlasViews = !allViews.empty();
        bool hasPointShadows = !pointShadowViews.empty();

        if (!hasAtlasViews && !hasPointShadows)
            return;

        // Validate batch parameters to prevent out-of-bounds access
        if (params.batchCount == 0 || params.commandsPerSection == 0)
            return;

        if (!params.drawCommandBuffer || !params.drawCountBuffer)
        {
            spdlog::warn("ShadowSystem::recordShadowPass: Invalid draw buffers");
            return;
        }

#ifndef NDEBUG
        // Debug validation for descriptor sets
        if (!params.perDrawDataDescSet || !params.meshletDataDescSet ||
            !params.vertexDataDescSet || !params.boneMatrixDescSet)
        {
            spdlog::error("ShadowSystem::recordShadowPass: Invalid descriptor set(s) - "
                          "perDraw={}, meshlet={}, vertex={}, bone={}",
                          static_cast<bool>(params.perDrawDataDescSet),
                          static_cast<bool>(params.meshletDataDescSet),
                          static_cast<bool>(params.vertexDataDescSet),
                          static_cast<bool>(params.boneMatrixDescSet));
            return;
        }
#endif

        // ========================================
        // Atlas rendering (spot and directional lights)
        // ========================================
        if (hasAtlasViews)
        {
        // 1. Transition atlas to depth attachment
        // On first use, image is in eUndefined; after that it's in eShaderReadOnlyOptimal
        {
            vk::ImageMemoryBarrier barrier{};
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = atlasManager->getAtlasImage();
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vk::PipelineStageFlags srcStage;
            if (atlasFirstUse)
            {
                // First frame: image is in undefined layout, no prior access to wait for
                barrier.srcAccessMask = {};
                barrier.oldLayout = vk::ImageLayout::eUndefined;
                srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            }
            else
            {
                // Subsequent frames: image was used for shader reading
                barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                srcStage = vk::PipelineStageFlagBits::eFragmentShader;
            }

            cmd.pipelineBarrier(
                srcStage,
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

            // Set dynamic depth bias using per-light settings
            cmd.setDepthBias(view->depthBias, 0.0f, view->slopeBias);

            // Push constants with light view-projection matrix and per-light bias
            ShadowPushConstants pc{};
            pc.lightViewProjection = view->viewProjectionMatrix;
            pc.baseDrawIndex = 0;  // Will iterate through batches
            pc.depthBias = view->depthBias;
            pc.slopeBias = view->slopeBias;
            pc.normalBias = view->normalBias;

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

        // Mark atlas as initialized for subsequent frames
        atlasFirstUse = false;
        } // end if (hasAtlasViews)

        // ========================================
        // Point light cube shadow rendering
        // ========================================
        renderPointLightCubeShadows(cmd, params);
    }

    void ShadowSystem::renderPointLightCubeShadows(vk::CommandBuffer cmd, const ShadowPassParams& params)
    {
        if (!resourcePool || !shadowPassPipeline)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        // Collect point lights that need cube shadow rendering
        std::vector<std::pair<uint32_t, LightShadowData*>> pointLightsToRender;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;

            ShadowCubeMap* cube = resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            pointLightsToRender.emplace_back(entityId, &data);
        }

        if (pointLightsToRender.empty())
            return;

        // Collect all framebuffers for deferred destruction after command buffer execution
        // We store them in pendingCubeFramebuffers and destroy them next frame
        // (This is safe because by next frame, this frame's commands have completed)
        for (vk::Framebuffer fb : pendingCubeFramebuffers)
        {
            logicalDevice.destroyFramebuffer(fb);
        }
        pendingCubeFramebuffers.clear();

        // Process each point light
        for (auto& [entityId, data] : pointLightsToRender)
        {
            ShadowCubeMap* cube = resourcePool->getCube(data->resourceHandle);
            uint32_t cubeSize = cube->getSize();

            // Transition entire cube to depth attachment
            cube->transitionToDepthAttachment(cmd);

            // Render each of the 6 faces
            for (uint32_t face = 0; face < ShadowCubeMap::FACE_COUNT; ++face)
            {
                if (face >= data->views.size())
                    continue;

                const auto& view = data->views[face];

                // Create temporary framebuffer for this cube face
                vk::FramebufferCreateInfo fbInfo{};
                fbInfo.renderPass = shadowPassPipeline->getRenderPass();
                fbInfo.attachmentCount = 1;
                vk::ImageView faceView = cube->getFaceView(face);
                fbInfo.pAttachments = &faceView;
                fbInfo.width = cubeSize;
                fbInfo.height = cubeSize;
                fbInfo.layers = 1;

                vk::Framebuffer faceFramebuffer = logicalDevice.createFramebuffer(fbInfo);
                pendingCubeFramebuffers.push_back(faceFramebuffer);

                // Begin render pass for this face
                vk::ClearValue clearValue{};
                clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                vk::RenderPassBeginInfo renderPassInfo{};
                renderPassInfo.renderPass = shadowPassPipeline->getRenderPass();
                renderPassInfo.framebuffer = faceFramebuffer;
                renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
                renderPassInfo.renderArea.extent = vk::Extent2D{cubeSize, cubeSize};
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;

                cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

                // Bind pipeline
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

                // Bind descriptor sets
                std::array<vk::DescriptorSet, 4> descriptorSets = {
                    params.perDrawDataDescSet,
                    params.meshletDataDescSet,
                    params.vertexDataDescSet,
                    params.boneMatrixDescSet
                };
                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    shadowPassPipeline->getPipelineLayout(),
                    0,
                    static_cast<uint32_t>(descriptorSets.size()),
                    descriptorSets.data(),
                    0, nullptr
                );

                // Set viewport and scissor for full cube face
                vk::Viewport viewport{};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(cubeSize);
                viewport.height = static_cast<float>(cubeSize);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                cmd.setViewport(0, 1, &viewport);

                vk::Rect2D scissor{};
                scissor.offset = vk::Offset2D{0, 0};
                scissor.extent = vk::Extent2D{cubeSize, cubeSize};
                cmd.setScissor(0, 1, &scissor);

                // Set depth bias
                cmd.setDepthBias(view.depthBias, 0.0f, view.slopeBias);

                // Push constants
                ShadowPushConstants pc{};
                pc.lightViewProjection = view.viewProjectionMatrix;
                pc.depthBias = view.depthBias;
                pc.slopeBias = view.slopeBias;
                pc.normalBias = view.normalBias;

                // Render all batches
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

                cmd.endRenderPass();
            }

            // Transition cube back to shader read
            cube->transitionToShaderRead(cmd);
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

    // Frame time budget threshold for warning about expensive operations (16ms = 60fps frame)
    static constexpr float FRAME_BUDGET_WARNING_MS = 16.0f;

    void ShadowSystem::applyRenderSettings(const types::RenderSettings& settings)
    {
        if (!initialized)
        {
            spdlog::warn("ShadowSystem::applyRenderSettings() called when not initialized");
            return;
        }

        const auto& shadowSettings = settings.shadows;

        // Update enabled state
        setShadowsEnabled(shadowSettings.enabled);

        if (!shadowSettings.enabled || shadowSettings.quality == types::ShadowQuality::Off)
        {
            spdlog::info("ShadowSystem: Shadows disabled via RenderSettings");
            return;
        }

        // Get atlas config from quality or use direct config
        types::ShadowAtlasConfig atlasConfig = shadowSettings.atlas;
        if (atlasConfig.atlasSize == 0)
        {
            // If no explicit config, derive from quality
            atlasConfig = types::ShadowAtlasConfig::fromQuality(shadowSettings.quality);
        }

        spdlog::debug("ShadowSystem: Applying render settings - quality={}, atlasSize={}",
                      static_cast<int>(shadowSettings.quality), atlasConfig.atlasSize);

        // Check if atlas resize is needed
        bool needsResize = atlasManager &&
                           (atlasManager->getAtlasWidth() != atlasConfig.atlasSize ||
                            atlasManager->getAtlasHeight() != atlasConfig.atlasSize);

        if (needsResize)
        {
            auto resizeStartTime = std::chrono::high_resolution_clock::now();

            spdlog::info("ShadowSystem: Atlas resize required - {} -> {}",
                         atlasManager->getAtlasWidth(), atlasConfig.atlasSize);

            // Store existing light registrations
            struct LightRegInfo
            {
                uint32_t entityId;
                ShadowMapType type;
                ShadowSettings settings;
            };
            std::vector<LightRegInfo> existingLights;

            for (const auto& [entityId, data] : lightShadowData)
            {
                existingLights.push_back({entityId, data.type, data.settings});
            }

            // Free all shadow maps (but keep registration data)
            for (auto& [entityId, data] : lightShadowData)
            {
                freeShadowMaps(data);
            }

            // Clear tracked handles in atlas
            lightShadowData.clear();

            // Resize atlas
            auto resizeResult = atlasManager->applyQualitySettings(atlasConfig);
            if (!resizeResult.success)
            {
                spdlog::error("ShadowSystem: Atlas resize failed");
                return;
            }

            // Recreate framebuffer if shadow pass pipeline exists
            if (shadowPassPipeline && shadowPassPipeline->isInitialized())
            {
                shadowPassPipeline->createFramebuffer(
                    atlasManager->getAtlasImageView(),
                    atlasManager->getAtlasWidth(),
                    atlasManager->getAtlasHeight()
                );
            }

            // Re-register lights with new resolutions
            uint32_t registeredCount = 0;
            uint32_t failedCount = 0;

            for (const auto& info : existingLights)
            {
                ShadowSettings newSettings = info.settings;

                // Update resolution based on light type and new quality config
                switch (info.type)
                {
                    case ShadowMapType::DirectionalCSM:
                    case ShadowMapType::Directional2D:
                        newSettings.resolution = atlasConfig.directionalResolution;
                        break;
                    case ShadowMapType::Spot2D:
                        newSettings.resolution = atlasConfig.spotResolution;
                        break;
                    case ShadowMapType::PointCube:
                        newSettings.resolution = atlasConfig.pointResolution;
                        break;
                    default:
                        break;
                }

                // Update cascade count from render settings
                newSettings.cascadeCount = shadowSettings.cascadeCount;

                if (registerLight(info.entityId, info.type, newSettings))
                {
                    ++registeredCount;
                }
                else
                {
                    ++failedCount;
                    spdlog::warn("ShadowSystem: Failed to re-register light {} after resize", info.entityId);
                }
            }

            if (failedCount > 0)
            {
                spdlog::warn("ShadowSystem: Re-registered {}/{} lights after atlas resize ({} failed)",
                             registeredCount, existingLights.size(), failedCount);
            }
            else
            {
                spdlog::info("ShadowSystem: Re-registered all {} lights after atlas resize",
                             existingLights.size());
            }

            // Measure and warn about expensive resize operations
            auto resizeEndTime = std::chrono::high_resolution_clock::now();
            float resizeMs = std::chrono::duration<float, std::milli>(resizeEndTime - resizeStartTime).count();
            if (resizeMs > FRAME_BUDGET_WARNING_MS)
            {
                spdlog::warn("ShadowSystem: Atlas resize took {:.1f}ms (exceeds {:.0f}ms frame budget)",
                             resizeMs, FRAME_BUDGET_WARNING_MS);
            }
            else
            {
                spdlog::debug("ShadowSystem: Atlas resize completed in {:.1f}ms", resizeMs);
            }
        }
        else
        {
            // No resize needed, just update quality and bias settings
            globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);

            for (auto& [entityId, data] : lightShadowData)
            {
                // Update bias settings
                data.settings.depthBias = shadowSettings.shadowBias;
                data.settings.normalBias = shadowSettings.normalBias;

                // Update cascade count if changed (requires reallocation for CSM)
                // NOTE: This is an expensive operation that reallocates shadow maps
                if (data.type == ShadowMapType::DirectionalCSM &&
                    data.settings.cascadeCount != shadowSettings.cascadeCount)
                {
                    auto cascadeStartTime = std::chrono::high_resolution_clock::now();

                    freeShadowMaps(data);
                    data.settings.cascadeCount = shadowSettings.cascadeCount;
                    data.views.resize(shadowSettings.cascadeCount);
                    allocateShadowMaps(data);

                    auto cascadeEndTime = std::chrono::high_resolution_clock::now();
                    float cascadeMs = std::chrono::duration<float, std::milli>(cascadeEndTime - cascadeStartTime).count();
                    if (cascadeMs > FRAME_BUDGET_WARNING_MS)
                    {
                        spdlog::warn("ShadowSystem: Cascade reallocation for light {} took {:.1f}ms (exceeds frame budget)",
                                     entityId, cascadeMs);
                    }
                }

                data.settingsDirty = true;
            }
        }

        // Reset atlas first use flag to ensure proper layout transitions
        atlasFirstUse = true;
        needsUpdate = true;

        // Apply PCF filtering settings
        globalPcfKernel = static_cast<uint8_t>(shadowSettings.pcfKernelSize);
        globalSoftShadowsEnabled = shadowSettings.softShadowsEnabled;

        spdlog::debug("ShadowSystem: PCF settings - kernel={}, softShadows={}",
                      globalPcfKernel, globalSoftShadowsEnabled);

        spdlog::info("ShadowSystem: Render settings applied successfully");
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

    std::vector<ShadowDebugInfo> ShadowSystem::getShadowDebugInfo() const
    {
        std::vector<ShadowDebugInfo> debugInfos;

        if (!shadowsEnabled)
            return debugInfos;

        // Reserve approximate capacity
        debugInfos.reserve(
            directionalShadowViews.size() +
            pointShadowViews.size() +
            spotShadowViews.size()
        );

        // Collect directional shadow debug info (CSM cascades)
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::DirectionalCSM || data.type == ShadowMapType::Directional2D)
            {
                for (size_t i = 0; i < data.views.size(); ++i)
                {
                    const auto& view = data.views[i];
                    ShadowDebugInfo info;
                    info.type = data.type;
                    info.cascadeIndex = static_cast<uint32_t>(i);
                    info.entityId = entityId;
                    info.viewProjectionMatrix = view.viewProjectionMatrix;
                    info.lightPosition = glm::vec3(view.lightPosition);
                    info.lightDirection = glm::vec3(view.lightDirection);
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;
                    debugInfos.push_back(info);
                }
            }
        }

        // Collect point light shadow debug info (spheres)
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
            {
                // Point lights use cube maps - we just need one debug info per light
                // showing the sphere radius (farPlane)
                if (!data.views.empty())
                {
                    const auto& view = data.views[0];
                    ShadowDebugInfo info;
                    info.type = data.type;
                    info.cascadeIndex = 0;
                    info.entityId = entityId;
                    info.viewProjectionMatrix = view.viewProjectionMatrix;
                    info.lightPosition = glm::vec3(view.lightPosition);
                    info.lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);  // Not applicable for point
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;  // This is the sphere radius
                    debugInfos.push_back(info);
                }
            }
        }

        // Collect spot light shadow debug info (frustums)
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::Spot2D)
            {
                if (!data.views.empty())
                {
                    const auto& view = data.views[0];
                    ShadowDebugInfo info;
                    info.type = data.type;
                    info.cascadeIndex = 0;
                    info.entityId = entityId;
                    info.viewProjectionMatrix = view.viewProjectionMatrix;
                    info.lightPosition = glm::vec3(view.lightPosition);
                    info.lightDirection = glm::vec3(view.lightDirection);
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;
                    debugInfos.push_back(info);
                }
            }
        }

        return debugInfos;
    }
}
