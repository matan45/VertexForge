#include "ShadowSystem.hpp"
#include "CascadeShadowCalculator.hpp"
#include "PointShadowCalculator.hpp"
#include "SpotShadowCalculator.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Logger.hpp"
#include <chrono>

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
            loggerWarning("ShadowSystem::init() called when already initialized");
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
            loggerError("ShadowSystem::initShadowPass() called before init()");
            return;
        }

        if (shadowPassPipeline)
        {
            loggerWarning("ShadowSystem::initShadowPass() called when already initialized");
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

    void ShadowSystem::initTerrainShadowPass(vk::DescriptorSetLayout terrainDataLayout,
                                              vk::DescriptorSetLayout terrainMeshletLayout,
                                              vk::DescriptorSetLayout terrainVertexLayout)
    {
        if (!initialized)
        {
            loggerError("ShadowSystem::initTerrainShadowPass() called before init()");
            return;
        }

        if (!shadowPassPipeline || !shadowPassPipeline->isInitialized())
        {
            loggerError("ShadowSystem::initTerrainShadowPass() called before initShadowPass()");
            return;
        }

        if (terrainShadowPipeline)
        {
            loggerWarning("ShadowSystem::initTerrainShadowPass() called when already initialized");
            return;
        }

        terrainShadowPipeline = std::make_unique<TerrainShadowPipeline>(device);
        terrainShadowPipeline->init(terrainDataLayout, terrainMeshletLayout, terrainVertexLayout,
                                     shadowPassPipeline->getRenderPass());

        loggerInfo("ShadowSystem: Terrain shadow pass initialized");
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
            loggerError("ShadowSystem: Failed to allocate shadow maps for light {}", entityId);
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
            loggerWarning("ShadowSystem: Attempted to unregister unknown light {}", entityId);
            return;
        }

        // Free shadow resources - uses deferred deletion queue if configured,
        // otherwise caller must ensure GPU synchronization
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
                    loggerError("ShadowSystem: Atlas manager not available for CSM allocation");
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
                        loggerError("ShadowSystem: Failed to allocate CSM cascade {} in atlas", i);
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
                    loggerError("ShadowSystem: Failed to allocate point cube map {}x{}", resolution, resolution);
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

    void ShadowSystem::beginFrame(const glm::mat4& cameraView,
                                  const glm::mat4& cameraProjection,
                                  float cameraNear,
                                  float cameraFar,
                                  const std::unordered_set<uint32_t>* visibleLightIds)
    {
        if (!shadowsEnabled)
            return;

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
            {
                auto entity = static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<components::WorldTransformComponent>(entity))
                    continue;

                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                glm::vec3 lightPosition = glm::vec3(worldTransform.worldMatrix[3]);

                float farPlane = data.settings.farPlane;
                if (registry.all_of<components::PointLightComponent>(entity))
                {
                    const auto& pointLight = registry.get<components::PointLightComponent>(entity);
                    farPlane = pointLight.radius;
                }
                float nearPlane = data.settings.nearPlane;

                auto faceMatrices = PointShadowCalculator::computeCubeFaceMatrices(
                    lightPosition, nearPlane, farPlane);

                for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT && face < data.views.size(); ++face)
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

                    view.depthBias = data.settings.depthBias;
                    view.slopeBias = data.settings.slopeBias;
                    view.normalBias = data.settings.normalBias;
                }
            }

            if (data.type == ShadowMapType::Spot2D)
            {
                auto entity = static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<components::WorldTransformComponent>(entity))
                    continue;

                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);
                glm::vec3 lightPosition = glm::vec3(worldTransform.worldMatrix[3]);

                glm::vec3 lightDirection = glm::normalize(
                    glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
                );

                float outerAngle = 45.0f;
                float range = 20.0f;
                if (registry.all_of<components::SpotLightComponent>(entity))
                {
                    const auto& spotLight = registry.get<components::SpotLightComponent>(entity);
                    outerAngle = spotLight.outerAngle;
                    range = spotLight.range;
                }

                float nearPlane = data.settings.nearPlane;

                auto shadowData = SpotShadowCalculator::computeSpotLightMatrices(
                    lightPosition, lightDirection, outerAngle, nearPlane, range);

                if (!data.views.empty())
                {
                    auto& view = data.views[0];
                    view.viewMatrix = shadowData.viewMatrix;
                    view.projectionMatrix = shadowData.projMatrix;
                    view.viewProjectionMatrix = shadowData.viewProjMatrix;
                    view.nearPlane = nearPlane;
                    view.farPlane = range;
                    view.lightPosition = glm::vec4(lightPosition, 1.0f);
                    view.lightDirection = glm::vec4(lightDirection, 0.0f);

                    view.depthBias = data.settings.depthBias;
                    view.slopeBias = data.settings.slopeBias;
                    view.normalBias = data.settings.normalBias;
                }
            }

            if (data.type == ShadowMapType::DirectionalCSM)
            {
                auto entity = static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<components::WorldTransformComponent>(entity))
                {
                    loggerWarning("ShadowSystem: DirectionalCSM light {} missing WorldTransformComponent", entityId);
                    continue;
                }

                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

                glm::vec3 lightDirection = glm::normalize(
                    glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
                );

                auto splits = CascadeShadowCalculator::computeSplitDistances(
                    cameraNear, cameraFar,
                    data.settings.cascadeCount,
                    globalCascadeSplitMode,
                    data.settings.cascadeSplitLambda
                );

                uint32_t viewCount = std::min(static_cast<uint32_t>(data.views.size()),
                                              data.settings.cascadeCount);

                for (uint32_t i = 0; i < viewCount; ++i)
                {
                    auto& view = data.views[i];

                    float cascadeNear = splits[i];
                    float cascadeFar = splits[i + 1];

                    auto frustumCorners = CascadeShadowCalculator::getFrustumCornersWorldSpace(
                        cameraView, cameraProjection, cascadeNear, cascadeFar);

                    auto cascadeData = CascadeShadowCalculator::computeCascadeMatrix(
                        frustumCorners, lightDirection, data.settings.resolution);

                    view.viewMatrix = cascadeData.viewMatrix;
                    view.projectionMatrix = cascadeData.projMatrix;
                    view.viewProjectionMatrix = cascadeData.viewProjMatrix;
                    // Store camera-space cascade split distances for cascade selection in shader
                    // (not light-space projection bounds which are only used for rendering)
                    view.nearPlane = cascadeNear;
                    view.farPlane = cascadeFar;
                    view.lightDirection = glm::vec4(lightDirection, 0.0f);
                    view.handle.cascadeIndex = static_cast<uint16_t>(i);

                    view.depthBias = data.settings.depthBias;
                    view.slopeBias = data.settings.slopeBias;
                    view.normalBias = data.settings.normalBias;
                }
            }
        }

        std::unordered_map<uint32_t, int32_t> directionalIndices;
        std::unordered_map<uint32_t, int32_t> pointIndices;
        std::unordered_map<uint32_t, int32_t> spotIndices;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            bool isDirectional = (data.type == ShadowMapType::DirectionalCSM ||
                data.type == ShadowMapType::Directional2D);
            if (visibleLightIds && !isDirectional && !visibleLightIds->contains(entityId))
                continue;

            switch (data.type)
            {
            case ShadowMapType::Directional2D:
                {
                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);
                    for (const auto& view : data.views)
                    {
                        if (!view.handle.isValid())
                            continue;

                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
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
                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);

                    for (size_t i = 0; i < data.views.size(); ++i)
                    {
                        const auto& view = data.views[i];
                        if (!view.handle.isValid())
                        {
                            loggerWarning("ShadowSystem: CSM cascade {} has invalid handle", i);
                            continue;
                        }

                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
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

            case ShadowMapType::Spot2D:
                {
                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);
                    for (const auto& view : data.views)
                    {
                        if (!view.handle.isValid())
                            continue;

                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
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
                    viewCopy.entityId = entityId;

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

    void ShadowSystem::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !shadowsEnabled || !gpuDataManager)
            return;

        std::unordered_map<uint32_t, uint32_t> entityToCubeIndex;
        uint32_t cubeIdx = 0;
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

        gpuDataManager->buildGPUShadowData(
            directionalShadowViews, pointShadowViews, spotShadowViews,
            lightShadowData, entityToCubeIndex);

        gpuDataManager->uploadToGPU(cmd);
        gpuDataManager->updateShadowTextureDescriptor(atlasManager.get(), resourcePool.get(), lightShadowData);

        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd,
                                         const ShadowPassParams& params,
                                         const TerrainShadowPassParams* terrainParams)
    {
        if (!passRecorder)
            return;

        passRecorder->recordShadowPass(cmd, params, terrainParams, atlasManager.get(), resourcePool.get(),
            shadowPassPipeline.get(), terrainShadowPipeline.get(),
            directionalShadowViews, spotShadowViews, lightShadowData, shadowsEnabled);
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

    void ShadowSystem::applyRenderSettings(const types::RenderSettings& settings)
    {
        if (!initialized)
        {
            loggerWarning("ShadowSystem::applyRenderSettings() called when not initialized");
            return;
        }

        const auto& shadowSettings = settings.shadows;

        shadowsEnabled = shadowSettings.enabled;
        needsUpdate = true;
        globalDepthBias = shadowSettings.shadowBias;
        globalSlopeBias = shadowSettings.slopeBias;
        globalNormalBias = shadowSettings.normalBias;
        globalCascadeCount = shadowSettings.cascadeCount;
        globalCascadeSplitMode = shadowSettings.cascadeSplitMode;

        if (!shadowSettings.enabled || shadowSettings.quality == types::ShadowQuality::Off)
            return;

        types::ShadowAtlasConfig atlasConfig = shadowSettings.atlas;
        if (atlasConfig.atlasSize == 0)
        {
            atlasConfig = types::ShadowAtlasConfig::fromQuality(shadowSettings.quality);
        }

        bool needsResize = atlasManager &&
        (atlasManager->getAtlasWidth() != atlasConfig.atlasSize ||
            atlasManager->getAtlasHeight() != atlasConfig.atlasSize);

        if (needsResize)
        {
            auto resizeStartTime = std::chrono::high_resolution_clock::now();

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

            for (auto& [entityId, data] : lightShadowData)
            {
                freeShadowMaps(data);
            }

            lightShadowData.clear();

            auto resizeResult = atlasManager->applyQualitySettings(atlasConfig);
            if (!resizeResult.success)
            {
                loggerError("ShadowSystem: Atlas resize failed");
                return;
            }

            if (shadowPassPipeline && shadowPassPipeline->isInitialized())
            {
                shadowPassPipeline->createFramebuffer(
                    atlasManager->getAtlasImageView(),
                    atlasManager->getAtlasWidth(),
                    atlasManager->getAtlasHeight()
                );
            }

            uint32_t registeredCount = 0;
            uint32_t failedCount = 0;

            for (const auto& info : existingLights)
            {
                ShadowSettings newSettings = info.settings;

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

                newSettings.cascadeCount = shadowSettings.cascadeCount;
                newSettings.depthBias = shadowSettings.shadowBias;
                newSettings.slopeBias = shadowSettings.slopeBias;
                newSettings.normalBias = shadowSettings.normalBias;

                if (registerLight(info.entityId, info.type, newSettings))
                {
                    ++registeredCount;
                }
                else
                {
                    ++failedCount;
                    loggerWarning("ShadowSystem: Failed to re-register light {} after resize", info.entityId);
                }
            }

            if (failedCount > 0)
            {
                loggerWarning("ShadowSystem: Re-registered {}/{} lights after atlas resize ({} failed)",
                             registeredCount, existingLights.size(), failedCount);
            }

            auto resizeEndTime = std::chrono::high_resolution_clock::now();
            float resizeMs = std::chrono::duration<float, std::milli>(resizeEndTime - resizeStartTime).count();
            if (resizeMs > FRAME_BUDGET_WARNING_MS)
            {
                loggerWarning("ShadowSystem: Atlas resize took {:.1f}ms (exceeds {:.0f}ms frame budget)",
                             resizeMs, FRAME_BUDGET_WARNING_MS);
            }
        }
        else
        {
            globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);

            for (auto& [entityId, data] : lightShadowData)
            {
                data.settings.depthBias = shadowSettings.shadowBias;
                data.settings.slopeBias = shadowSettings.slopeBias;
                data.settings.normalBias = shadowSettings.normalBias;

                if (data.type == ShadowMapType::DirectionalCSM &&
                    data.settings.cascadeCount != shadowSettings.cascadeCount)
                {
                    auto cascadeStartTime = std::chrono::high_resolution_clock::now();

                    freeShadowMaps(data);
                    data.settings.cascadeCount = shadowSettings.cascadeCount;
                    data.views.resize(shadowSettings.cascadeCount);
                    allocateShadowMaps(data);

                    auto cascadeEndTime = std::chrono::high_resolution_clock::now();
                    float cascadeMs = std::chrono::duration<float, std::milli>(cascadeEndTime - cascadeStartTime).
                        count();
                    if (cascadeMs > FRAME_BUDGET_WARNING_MS)
                    {
                        loggerWarning(
                            "ShadowSystem: Cascade reallocation for light {} took {:.1f}ms (exceeds frame budget)",
                            entityId, cascadeMs);
                    }
                }

                data.settingsDirty = true;
            }
        }

        if (passRecorder)
            passRecorder->resetAtlasFirstUse();
        needsUpdate = true;

        globalPcfKernel = static_cast<uint8_t>(shadowSettings.pcfKernelSize);
        globalSoftShadowsEnabled = shadowSettings.softShadowsEnabled;
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
