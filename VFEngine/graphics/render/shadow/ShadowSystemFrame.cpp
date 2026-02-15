#include "ShadowSystem.hpp"
#include "CascadeShadowCalculator.hpp"
#include "PointShadowCalculator.hpp"
#include "SpotShadowCalculator.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Logger.hpp"
#include <chrono>

namespace render::shadow
{
    void ShadowSystem::updatePointCubeShadowMatrices(LightShadowData& data, uint32_t entityId)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
            return;

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

    void ShadowSystem::updateSpotShadowMatrices(LightShadowData& data, uint32_t entityId)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (!registry.valid(entity) ||
            !registry.all_of<components::WorldTransformComponent>(entity))
            return;

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
            loggerWarning("ShadowSystem: DirectionalCSM light {} missing WorldTransformComponent", entityId);
            return;
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

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
                updatePointCubeShadowMatrices(data, entityId);

            if (data.type == ShadowMapType::Spot2D)
                updateSpotShadowMatrices(data, entityId);

            if (data.type == ShadowMapType::DirectionalCSM)
                updateDirectionalCSMMatrices(data, entityId, cameraView, cameraProjection, cameraNear, cameraFar);
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

        passRecorder->recordShadowPass(cmd, params, terrainParams, atlasManager.get(), resourcePool.get(),
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
}
