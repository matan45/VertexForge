#include "ShadowSystem.hpp"
#include "CascadeShadowCalculator.hpp"
#include "PointShadowCalculator.hpp"
#include "SpotShadowCalculator.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>
#include <future>

namespace render::shadow
{
    void ShadowSystem::updatePointCubeShadowMatrices(LightShadowData& data, uint32_t entityId)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
            return;

        updatePointCubeShadowMatricesFromData(data,
            registry.get<components::WorldTransformComponent>(entity).worldMatrix,
            registry.all_of<components::PointLightComponent>(entity)
                ? registry.get<components::PointLightComponent>(entity).radius
                : data.settings.farPlane);
    }

    void ShadowSystem::updatePointCubeShadowMatricesFromData(LightShadowData& data,
                                                              const glm::mat4& worldMatrix,
                                                              float radius)
    {
        glm::vec3 lightPosition = glm::vec3(worldMatrix[3]);

        float farPlane = radius;
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

    void ShadowSystem::updateSpotShadowMatrices(LightShadowData& data, uint32_t entityId)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
            return;

        float outerAngle = 45.0f;
        float range = 20.0f;
        if (registry.all_of<components::SpotLightComponent>(entity))
        {
            const auto& spotLight = registry.get<components::SpotLightComponent>(entity);
            outerAngle = spotLight.outerAngle;
            range = spotLight.range;
        }

        updateSpotShadowMatricesFromData(data,
            registry.get<components::WorldTransformComponent>(entity).worldMatrix,
            outerAngle, range);
    }

    void ShadowSystem::updateSpotShadowMatricesFromData(LightShadowData& data,
                                                         const glm::mat4& worldMatrix,
                                                         float outerAngle, float range)
    {
        glm::vec3 lightPosition = glm::vec3(worldMatrix[3]);

        glm::vec3 lightDirection = glm::normalize(
            glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
        );

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

    void ShadowSystem::updateDirectionalCSMMatrices(LightShadowData& data, uint32_t entityId,
                                                     const glm::mat4& cameraView,
                                                     const glm::mat4& cameraProjection,
                                                     float cameraNear, float cameraFar)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
        {
            vfLogWarning("ShadowSystem: DirectionalCSM light {} missing WorldTransformComponent", entityId);
            return;
        }

        updateDirectionalCSMMatricesFromData(data,
            registry.get<components::WorldTransformComponent>(entity).worldMatrix,
            cameraView, cameraProjection, cameraNear, cameraFar);
    }

    void ShadowSystem::updateDirectionalCSMMatricesFromData(LightShadowData& data,
                                                             const glm::mat4& worldMatrix,
                                                             const glm::mat4& cameraView,
                                                             const glm::mat4& cameraProjection,
                                                             float cameraNear, float cameraFar)
    {
        glm::vec3 lightDirection = glm::normalize(
            glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))
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
            view.nearPlane = cascadeNear;
            view.farPlane = cascadeFar;
            view.lightDirection = glm::vec4(lightDirection, 0.0f);
            view.handle.cascadeIndex = static_cast<uint16_t>(i);

            view.depthBias = data.settings.depthBias;
            view.slopeBias = data.settings.slopeBias;
            view.normalBias = data.settings.normalBias;
        }
    }

    void ShadowSystem::collectShadowViewsForGPU(const std::unordered_set<uint32_t>* visibleLightIds)
    {
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
                            vfLogWarning("ShadowSystem: CSM cascade {} has invalid handle", i);
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
                    if (!data.resourceHandle.isValid() || data.views.empty())
                        continue;

                    float texelSize = 1.0f / static_cast<float>(data.settings.resolution);

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

    void ShadowSystem::beginFrame(const glm::mat4& cameraView,
                                  const glm::mat4& cameraProjection,
                                  float cameraNear,
                                  float cameraFar,
                                  const std::unordered_set<uint32_t>* visibleLightIds)
    {
        if (!shadowsEnabled)
            return;

        ++frameCounter;

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();

        // Reset per-frame cache stats
        lastCacheStats = {};

        // Pre-collect entity data on main thread (EnTT registry is not thread-safe),
        // then dispatch pure computation in parallel.
        auto& registry = scene::EntityRegistry::getRegistry();

        struct PointLightRef { LightShadowData* data; glm::mat4 worldMatrix; float radius; };
        struct SpotLightRef { LightShadowData* data; glm::mat4 worldMatrix; float outerAngle; float range; };
        struct DirLightRef { LightShadowData* data; glm::mat4 worldMatrix; };

        std::vector<PointLightRef> pointLights;
        std::vector<SpotLightRef> spotLights;
        std::vector<DirLightRef> directionalLights;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity) || !registry.all_of<components::WorldTransformComponent>(entity))
                continue;

            // Track static light stats
            if (data.isStatic)
                ++lastCacheStats.totalStaticLights;

            // Skip matrix computation for cached static point/spot lights
            // (Directional CSM always needs update because cascades depend on camera position)
            if (data.isStatic && data.shadowCached && data.type != ShadowMapType::DirectionalCSM)
            {
                // Mark all views as cached so the recorder skips them
                for (auto& view : data.views)
                    view.cached = true;

                ++lastCacheStats.cachedShadowMaps;
                ++lastCacheStats.skippedThisFrame;
                continue;
            }

            const auto& worldMatrix = registry.get<components::WorldTransformComponent>(entity).worldMatrix;

            if (data.type == ShadowMapType::PointCube)
            {
                float radius = data.settings.farPlane;
                if (registry.all_of<components::PointLightComponent>(entity))
                    radius = registry.get<components::PointLightComponent>(entity).radius;
                pointLights.push_back({&data, worldMatrix, radius});
            }
            else if (data.type == ShadowMapType::Spot2D)
            {
                float outerAngle = 45.0f, range = 20.0f;
                if (registry.all_of<components::SpotLightComponent>(entity))
                {
                    const auto& spotLight = registry.get<components::SpotLightComponent>(entity);
                    outerAngle = spotLight.outerAngle;
                    range = spotLight.range;
                }
                spotLights.push_back({&data, worldMatrix, outerAngle, range});
            }
            else if (data.type == ShadowMapType::DirectionalCSM)
            {
                directionalLights.push_back({&data, worldMatrix});
            }

            // Mark views as not cached (will be rendered this frame)
            for (auto& view : data.views)
                view.cached = false;
            ++lastCacheStats.renderedThisFrame;
        }

        auto f1 = threading::JobSystem::instance().submit(
            [this, &pointLights]() {
                for (auto& ref : pointLights)
                    updatePointCubeShadowMatricesFromData(*ref.data, ref.worldMatrix, ref.radius);
            }, threading::JobPriority::HIGH
        );
        auto f2 = threading::JobSystem::instance().submit(
            [this, &spotLights]() {
                for (auto& ref : spotLights)
                    updateSpotShadowMatricesFromData(*ref.data, ref.worldMatrix, ref.outerAngle, ref.range);
            }, threading::JobPriority::HIGH
        );
        auto f3 = threading::JobSystem::instance().submit(
            [this, &directionalLights, &cameraView, &cameraProjection, cameraNear, cameraFar]() {
                for (auto& ref : directionalLights)
                    updateDirectionalCSMMatricesFromData(*ref.data, ref.worldMatrix, cameraView, cameraProjection, cameraNear, cameraFar);
            }, threading::JobPriority::HIGH
        );

        f1.get();
        f2.get();
        f3.get();

        // Mark static point/spot lights as cached after matrices are computed
        // Require at least 2 rendered frames before caching, so the shadow map
        // is fully rendered (first frame may have incomplete state)
        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic && !data.shadowCached &&
                data.settings.enabled && data.settings.castShadows &&
                data.type != ShadowMapType::DirectionalCSM)
            {
                ++data.renderedFrameCount;
                if (data.renderedFrameCount >= 2)
                {
                    data.shadowCached = true;
                    data.lastRenderedFrame = frameCounter;
                }
            }
        }

        collectShadowViewsForGPU(visibleLightIds);
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

        passRecorder->recordShadowPass(cmd, params, terrainParams,
            atlasManager.get(), resourcePool.get(),
            shadowPassPipeline.get(), terrainShadowPipeline.get(),
            directionalShadowViews, spotShadowViews, lightShadowData, shadowsEnabled);
    }

    void ShadowSystem::handleAtlasResize(const types::RenderSettings& settings,
                                          const types::ShadowAtlasConfig& atlasConfig)
    {
        auto resizeStartTime = std::chrono::high_resolution_clock::now();
        const auto& shadowSettings = settings.shadows;

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
            vfLogError("ShadowSystem: Atlas resize failed");
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
                vfLogWarning("ShadowSystem: Failed to re-register light {} after resize", info.entityId);
            }
        }

        if (failedCount > 0)
        {
            vfLogWarning("ShadowSystem: Re-registered {}/{} lights after atlas resize ({} failed)",
                         registeredCount, existingLights.size(), failedCount);
        }

        auto resizeEndTime = std::chrono::high_resolution_clock::now();
        float resizeMs = std::chrono::duration<float, std::milli>(resizeEndTime - resizeStartTime).count();
        if (resizeMs > FRAME_BUDGET_WARNING_MS)
        {
            vfLogWarning("ShadowSystem: Atlas resize took {:.1f}ms (exceeds {:.0f}ms frame budget)",
                         resizeMs, FRAME_BUDGET_WARNING_MS);
        }
    }

    void ShadowSystem::applyRenderSettings(const types::RenderSettings& settings)
    {
        if (!initialized)
        {
            vfLogWarning("ShadowSystem::applyRenderSettings() called when not initialized");
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
            handleAtlasResize(settings, atlasConfig);
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
                        vfLogWarning(
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

        // Apply shadow LOD settings
        shadowLODConfig.enabled = settings.shadowLOD.enabled;
        shadowLODConfig.tier0Distance = settings.shadowLOD.tier0Distance;
        shadowLODConfig.tier1Distance = settings.shadowLOD.tier1Distance;
        shadowLODConfig.tier2Distance = settings.shadowLOD.tier2Distance;
        shadowLODConfig.tier0Resolution = settings.shadowLOD.tier0Resolution;
        shadowLODConfig.tier1Resolution = settings.shadowLOD.tier1Resolution;
        shadowLODConfig.tier2Resolution = settings.shadowLOD.tier2Resolution;
    }

    void ShadowSystem::applyShadowLODSettings(const ShadowLODConfig& config)
    {
        shadowLODConfig = config;
    }

    void ShadowSystem::updateShadowLOD(const glm::vec3& cameraPosition)
    {
        if (!initialized || !shadowLODConfig.enabled)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::DirectionalCSM)
            {
                continue; // Directional lights always keep their resolution
            }

            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity) || !registry.all_of<components::WorldTransformComponent>(entity))
            {
                continue;
            }

            const auto& transform = registry.get<components::WorldTransformComponent>(entity);
            glm::vec3 lightPos = glm::vec3(transform.worldMatrix[3]);
            float distance = glm::distance(cameraPosition, lightPos);

            uint32_t desiredResolution = data.isStatic
                ? shadowLODConfig.getResolutionForStaticLight(distance)
                : shadowLODConfig.getResolutionForDistance(distance);
            bool shouldHaveShadow = data.isStatic
                ? shadowLODConfig.shouldStaticHaveShadow(distance)
                : shadowLODConfig.shouldHaveShadow(distance);

            auto currentResIt = currentShadowResolutions.find(entityId);
            uint32_t currentRes = (currentResIt != currentShadowResolutions.end())
                ? currentResIt->second : data.settings.resolution;

            if (!shouldHaveShadow && data.resourceHandle.isValid())
            {
                // Light too far, remove shadow
                freeShadowMaps(data);
                currentShadowResolutions.erase(entityId);
                needsUpdate = true;
            }
            else if (shouldHaveShadow && desiredResolution != currentRes)
            {
                // Resolution change needed
                if (data.resourceHandle.isValid())
                {
                    freeShadowMaps(data);
                }

                data.settings.resolution = desiredResolution;
                if (allocateShadowMaps(data))
                {
                    currentShadowResolutions[entityId] = desiredResolution;
                    data.settingsDirty = true;
                    data.invalidateCache(); // Force re-render at new resolution
                    needsUpdate = true;
                }
            }
        }
    }
}
