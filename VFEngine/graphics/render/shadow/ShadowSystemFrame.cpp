#include "ShadowSystem.hpp"
#include "CascadeShadowCalculator.hpp"
#include "PointShadowCalculator.hpp"
#include "SpotShadowCalculator.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"
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
            view.layer = face;
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
            glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

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
                                                     const CameraContext& camera)
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
            registry.get<components::WorldTransformComponent>(entity).worldMatrix, camera);
    }

    void ShadowSystem::updateDirectionalCSMMatricesFromData(LightShadowData& data,
                                                             const glm::mat4& worldMatrix,
                                                             const CameraContext& camera)
    {
        glm::vec3 lightDirection = glm::normalize(
            glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

        auto splits = CascadeShadowCalculator::computeSplitDistances(
            camera.nearPlane, camera.farPlane,
            data.settings.cascadeCount,
            globalCascadeSplitMode,
            data.settings.cascadeSplitLambda);

        uint32_t viewCount = std::min(static_cast<uint32_t>(data.views.size()),
                                      data.settings.cascadeCount);

        for (uint32_t i = 0; i < viewCount; ++i)
        {
            auto& view = data.views[i];
            float cascadeNear = splits[i];
            float cascadeFar = splits[i + 1];

            auto frustumCorners = CascadeShadowCalculator::getFrustumCornersWorldSpace(
                camera.view, camera.projection, cascadeNear, cascadeFar);

            auto cascadeData = CascadeShadowCalculator::computeCascadeMatrix(
                frustumCorners, lightDirection, data.settings.resolution);

            view.viewMatrix = cascadeData.viewMatrix;
            view.projectionMatrix = cascadeData.projMatrix;
            view.viewProjectionMatrix = cascadeData.viewProjMatrix;
            view.nearPlane = cascadeNear;
            view.farPlane = cascadeFar;
            view.lightDirection = glm::vec4(lightDirection, 0.0f);
            view.cascadeIndex = static_cast<uint16_t>(i);
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

            if (visibleLightIds && !data.isDirectionalType() && !visibleLightIds->contains(entityId))
                continue;

            float texelSize = 1.0f / static_cast<float>(data.settings.resolution);

            auto applyViewDefaults = [&](ShadowView& viewCopy) {
                viewCopy.entityId = entityId;
                viewCopy.depthBias = data.settings.depthBias;
                viewCopy.slopeBias = data.settings.slopeBias;
                viewCopy.normalBias = data.settings.normalBias;
                viewCopy.texelSize = texelSize;
                viewCopy.lightSize = data.settings.lightSize;
                viewCopy.filterEnabled = globalSoftShadows;
            };

            switch (data.type)
            {
            case ShadowMapType::Directional2D:
            case ShadowMapType::DirectionalCSM:
            case ShadowMapType::DirectionalClipmap:
                for (const auto& view : data.views)
                {
                    ShadowView viewCopy = view;
                    applyViewDefaults(viewCopy);
                    if (!directionalIndices.contains(entityId))
                        directionalIndices[entityId] = static_cast<int32_t>(directionalShadowViews.size());
                    directionalShadowViews.push_back(viewCopy);
                }
                break;

            case ShadowMapType::Spot2D:
                for (const auto& view : data.views)
                {
                    ShadowView viewCopy = view;
                    applyViewDefaults(viewCopy);
                    if (!spotIndices.contains(entityId))
                        spotIndices[entityId] = static_cast<int32_t>(spotShadowViews.size());
                    spotShadowViews.push_back(viewCopy);
                }
                break;

            case ShadowMapType::PointCube:
            {
                if (!data.resourceHandle.isValid() || data.views.empty())
                    continue;

                ShadowView viewCopy = data.views[0];
                applyViewDefaults(viewCopy);
                pointIndices[entityId] = static_cast<int32_t>(pointShadowViews.size());
                pointShadowViews.push_back(viewCopy);
                break;
            }

            default:
                break;
            }
        }

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

        glm::vec3 cameraPosition = -glm::vec3(cameraView[3]) * glm::mat3(cameraView);
        glm::vec3 cameraForward = -glm::vec3(cameraView[0][2], cameraView[1][2], cameraView[2][2]);
        cameraMovedThisFrame = glm::distance(cameraPosition, lastCameraPosition) > CAMERA_MOVE_EPSILON
                            || glm::distance(cameraForward, lastCameraForward) > CAMERA_MOVE_EPSILON;
        lastCameraPosition = cameraPosition;
        lastCameraForward = cameraForward;

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();
        pageRenderList.clear();

        updateStaticFlags();
        lastCacheStats = {};

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

            if (data.isStatic)
                ++lastCacheStats.totalStaticLights;

            if (data.isStatic && data.shadowCached && !data.isDirectionalType())
            {
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
            else if (data.isDirectionalType())
            {
                directionalLights.push_back({&data, worldMatrix});
            }

            for (auto& view : data.views)
                view.cached = false;
            ++lastCacheStats.renderedThisFrame;
        }

        auto f1 = threading::JobSystem::instance().submit(
            [this, &pointLights]() {
                for (auto& ref : pointLights)
                    updatePointCubeShadowMatricesFromData(*ref.data, ref.worldMatrix, ref.radius);
            }, threading::JobPriority::HIGH);
        auto f2 = threading::JobSystem::instance().submit(
            [this, &spotLights]() {
                for (auto& ref : spotLights)
                    updateSpotShadowMatricesFromData(*ref.data, ref.worldMatrix, ref.outerAngle, ref.range);
            }, threading::JobPriority::HIGH);
        CameraContext camera{cameraView, cameraProjection, cameraNear, cameraFar};
        auto f3 = threading::JobSystem::instance().submit(
            [this, &directionalLights, camera]() {
                for (auto& ref : directionalLights)
                {
                    if (ref.data->type == ShadowMapType::DirectionalClipmap)
                        updateDirectionalClipmapMatricesFromData(*ref.data, ref.worldMatrix, camera);
                    else
                        updateDirectionalCSMMatricesFromData(*ref.data, ref.worldMatrix, camera);
                }
            }, threading::JobPriority::HIGH);

        f1.get();
        f2.get();
        f3.get();

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            // Track rendered frames for scene-load warmup (forceRender in addPageToRenderLists).
            // Capped to avoid unbounded growth; only the first few frames matter.
            if (data.renderedFrameCount < 10)
                ++data.renderedFrameCount;

            if (data.isStatic && !data.shadowCached && !data.isDirectionalType())
            {
                if (data.renderedFrameCount >= 4)
                {
                    data.shadowCached = true;
                    data.lastRenderedFrame = frameCounter;
                }
            }
        }

        collectShadowViewsForGPU(visibleLightIds);
        applyFeedbackAllocations();
        determineDynamicPages();
        buildPageRenderList();
    }

    void ShadowSystem::updateDirectionalClipmapMatricesFromData(LightShadowData& data,
                                                                  const glm::mat4& worldMatrix,
                                                                  const CameraContext& camera)
    {
        glm::vec3 lightDirection = glm::normalize(
            glm::vec3(worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

        glm::vec3 cameraWorldPos = -glm::vec3(camera.view[3]) * glm::mat3(camera.view);

        uint32_t levelCount = std::min(static_cast<uint32_t>(data.views.size()),
                                        data.settings.clipmapLevelCount);

        for (uint32_t i = 0; i < levelCount; ++i)
        {
            uint32_t pagesPerSide = (i < data.clipmapLevelPagesPerSide.size())
                ? data.clipmapLevelPagesPerSide[i] : 1;
            uint32_t levelResolution = pagesPerSide * vsm::PAGE_SIZE;

            auto levelData = ClipmapShadowCalculator::computeClipmapLevel(
                i, data.settings.clipmapBaseExtent, cameraWorldPos, lightDirection, levelResolution);

            auto& view = data.views[i];
            view.viewMatrix = levelData.viewMatrix;
            view.projectionMatrix = levelData.projMatrix;
            view.viewProjectionMatrix = levelData.viewProjMatrix;
            view.nearPlane = levelData.nearDistance;
            view.farPlane = levelData.farDistance;
            view.lightDirection = glm::vec4(lightDirection, 0.0f);
            view.cascadeIndex = static_cast<uint16_t>(i);
            view.texelSize = levelData.texelSize;

            float biasScale = 1.0f + static_cast<float>(i) * 0.3f;
            view.depthBias = data.settings.depthBias * biasScale;
            view.slopeBias = data.settings.slopeBias * biasScale;
            view.normalBias = data.settings.normalBias * biasScale;

            updateClipmapDirtyFlags(data, i, levelData);
        }
    }

    void ShadowSystem::updateClipmapDirtyFlags(LightShadowData& data, uint32_t level,
                                                const ClipmapLevelData& levelData)
    {
        if (level >= data.clipmapLastSnapPositions.size())
            return;

        if (level >= data.clipmapLevelPageOffsets.size() || level >= data.clipmapLevelPagesPerSide.size())
            return;

        glm::ivec2 texelShift = ClipmapShadowCalculator::computeSnapDeltaTexels(
            levelData, data.clipmapLastSnapPositions[level]);

        if (texelShift.x == 0 && texelShift.y == 0)
            return; // No movement — all cached pages remain valid

        // When the clipmap snaps by any amount, the view-projection matrix changes.
        // Since each page's cropViewProjection = cropMatrix * VP, all pages become
        // stale and must be re-rendered. This is the minimal correct invalidation —
        // frames with zero snap delta skip entirely (the common case when the camera
        // moves less than one texel in light space).
        uint32_t basePageIdx = data.clipmapLevelPageOffsets[level];
        uint32_t pps = data.clipmapLevelPagesPerSide[level];
        for (uint32_t p = 0; p < pps * pps; ++p)
        {
            uint32_t pageIdx = basePageIdx + p;
            if (pageIdx < data.vsmPageDirty.size())
                data.vsmPageDirty[pageIdx] = true;
        }

        data.clipmapLastSnapPositions[level] = levelData.snapPosition;
    }
}
