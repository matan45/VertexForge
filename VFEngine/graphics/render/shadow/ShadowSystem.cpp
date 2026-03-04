#include "ShadowSystem.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    ShadowSystem::ShadowSystem(core::Device& device)
        : device(device)
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
            return;
        }

        atlasManager = std::make_unique<ShadowAtlasManager>(device);
        atlasManager->init();

        resourcePool = std::make_unique<ShadowResourcePool>(device);
        resourcePool->init();

        gpuDataManager = std::make_unique<ShadowGPUDataManager>(device);
        gpuDataManager->init();

        passRecorder = std::make_unique<ShadowPassRecorder>(device);

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
            passRecorder->resetAtlasFirstUse();
        }

        gpuDataManager->updateShadowTextureDescriptor(atlasManager.get(), resourcePool.get(), lightShadowData);

        initialized = true;
    }

    void ShadowSystem::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        if (shadowPassPipeline)
        {
            shadowPassPipeline->cleanup();
            shadowPassPipeline.reset();
        }

        if (terrainShadowPipeline)
        {
            terrainShadowPipeline->cleanup();
            terrainShadowPipeline.reset();
        }

        if (gpuDataManager)
        {
            gpuDataManager->cleanup();
            gpuDataManager.reset();
        }

        passRecorder.reset();

        lightShadowData.clear();
        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();

        if (resourcePool)
        {
            resourcePool->cleanup();
            resourcePool.reset();
        }

        if (atlasManager)
        {
            atlasManager->cleanup();
            atlasManager.reset();
        }

        initialized = false;
    }

    void ShadowSystem::initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                      vk::DescriptorSetLayout meshletDataLayout,
                                      vk::DescriptorSetLayout vertexDataLayout,
                                      vk::DescriptorSetLayout boneMatrixLayout)
    {
        if (!initialized)
        {
            vfLogError("ShadowSystem::initShadowPass() called before init()");
            return;
        }

        if (shadowPassPipeline)
        {
            return;
        }

        shadowPassPipeline = std::make_unique<ShadowPassPipeline>(device);
        shadowPassPipeline->init(perDrawLayout, meshletDataLayout, vertexDataLayout, boneMatrixLayout,
                                 atlasManager->getDepthFormat());

        shadowPassPipeline->createFramebuffer(
            atlasManager->getAtlasImageView(),
            atlasManager->getAtlasWidth(),
            atlasManager->getAtlasHeight()
        );
    }

    void ShadowSystem::updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize)
    {
        if (shadowPassPipeline && shadowPassPipeline->isInitialized())
        {
            shadowPassPipeline->updateCameraDescriptor(cameraBuffer, bufferSize);
        }
    }

    vk::DescriptorSet ShadowSystem::getShadowCameraDescSet() const
    {
        if (shadowPassPipeline && shadowPassPipeline->isInitialized())
        {
            return shadowPassPipeline->getCameraDescriptorSet();
        }
        return {};
    }

    void ShadowSystem::initTerrainShadowPass(vk::DescriptorSetLayout terrainDataLayout,
                                              vk::DescriptorSetLayout terrainMeshletLayout,
                                              vk::DescriptorSetLayout terrainVertexLayout)
    {
        if (!initialized)
        {
            vfLogError("ShadowSystem::initTerrainShadowPass() called before init()");
            return;
        }

        if (!shadowPassPipeline || !shadowPassPipeline->isInitialized())
        {
            vfLogError("ShadowSystem::initTerrainShadowPass() called before initShadowPass()");
            return;
        }

        if (terrainShadowPipeline)
        {
            return;
        }

        terrainShadowPipeline = std::make_unique<TerrainShadowPipeline>(device);
        terrainShadowPipeline->init(terrainDataLayout, terrainMeshletLayout, terrainVertexLayout,
                                     shadowPassPipeline->getRenderPass());

        vfLogInfo("ShadowSystem: Terrain shadow pass initialized");
    }

    bool ShadowSystem::registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings)
    {
        if (lightShadowData.contains(entityId))
        {
            auto& data = lightShadowData[entityId];
            data.settings = settings;
            data.settingsDirty = true;
            return true;
        }

        LightShadowData data;
        data.settings = settings;
        data.type = type;
        data.lightEntityId = entityId;
        data.matricesDirty = true;
        data.settingsDirty = true;

        uint32_t viewCount = 1;
        switch (type)
        {
        case ShadowMapType::DirectionalCSM:
            viewCount = settings.cascadeCount;
            break;
        case ShadowMapType::PointCube:
            viewCount = 6;
            break;
        case ShadowMapType::Spot2D:
        case ShadowMapType::Directional2D:
            viewCount = 1;
            break;
        default:
            break;
        }

        data.views.resize(viewCount);

        if (!allocateShadowMaps(data))
        {
            vfLogError("ShadowSystem: Failed to allocate shadow maps for light {}", entityId);
            return false;
        }

        lightShadowData[entityId] = std::move(data);
        needsUpdate = true;

        return true;
    }

    void ShadowSystem::unregisterLight(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        if (it == lightShadowData.end())
        {
            vfLogWarning("ShadowSystem: Attempted to unregister unknown light {}", entityId);
            return;
        }

        freeShadowMaps(it->second);
        lightShadowData.erase(it);
        needsUpdate = true;
    }

    void ShadowSystem::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (resourcePool)
        {
            resourcePool->setDeletionQueue(queue);
        }
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
                if (!atlasManager || !atlasManager->isInitialized())
                {
                    vfLogError("ShadowSystem: Atlas manager not available for CSM allocation");
                    return false;
                }

                for (size_t i = 0; i < data.views.size(); ++i)
                {
                    auto& view = data.views[i];

                    ShadowMapHandle handle = atlasManager->allocate(
                        resolution, resolution,
                        ShadowMapType::DirectionalCSM,
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
                        vfLogError("ShadowSystem: Failed to allocate CSM cascade {} in atlas", i);
                        return false;
                    }

                    view.handle = handle;
                    view.handle.cascadeIndex = static_cast<uint16_t>(i);
                    view.atlasViewport = atlasManager->getNormalizedViewport(handle);
                }

                return true;
            }

        case ShadowMapType::PointCube:
            {
                if (!resourcePool || !resourcePool->isInitialized())
                    return false;

                ShadowResourceHandle handle = resourcePool->allocateCube(resolution);

                if (!handle.isValid())
                {
                    vfLogError("ShadowSystem: Failed to allocate point cube map {}x{}", resolution, resolution);
                    return false;
                }

                data.resourceHandle = handle;

                for (size_t i = 0; i < data.views.size(); ++i)
                {
                    auto& view = data.views[i];
                    view.handle.type = ShadowMapType::PointCube;
                    view.handle.layer = static_cast<uint32_t>(i);
                    // For cubes, atlasViewport.w stores the face index
                    view.atlasViewport = glm::vec4(0.0f, 0.0f, 1.0f, static_cast<float>(i));
                }

                return true;
            }

        default:
            return false;
        }
    }

    void ShadowSystem::freeShadowMaps(LightShadowData& data)
    {
        if (data.resourceHandle.isValid() && resourcePool)
        {
            resourcePool->free(data.resourceHandle);
            data.resourceHandle.invalidate();
        }

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

    vk::DescriptorSetLayout ShadowSystem::getShadowDataLayout() const
    {
        return gpuDataManager ? gpuDataManager->getShadowDataLayout() : nullptr;
    }

    vk::DescriptorSet ShadowSystem::getShadowDataDescSet() const
    {
        return gpuDataManager ? gpuDataManager->getShadowDataDescSet() : nullptr;
    }

    vk::DescriptorSetLayout ShadowSystem::getShadowTextureLayout() const
    {
        return gpuDataManager ? gpuDataManager->getShadowTextureLayout() : nullptr;
    }

    vk::DescriptorSet ShadowSystem::getShadowTextureDescSet() const
    {
        return gpuDataManager ? gpuDataManager->getShadowTextureDescSet() : nullptr;
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

        debugInfos.reserve(
            directionalShadowViews.size() +
            pointShadowViews.size() +
            spotShadowViews.size()
        );

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

        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
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
                    info.lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;
                    debugInfos.push_back(info);
                }
            }
        }

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
