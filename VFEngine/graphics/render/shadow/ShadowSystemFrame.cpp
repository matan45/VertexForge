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
        std::unordered_map<uint32_t, int32_t> dirIdx, ptIdx, spotIdx;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;
            if (visibleLightIds && !data.isDirectionalType() && !visibleLightIds->contains(entityId))
                continue;

            float texelSize = 1.0f / static_cast<float>(data.settings.resolution);
            auto makeView = [&](const ShadowView& view) {
                ShadowView v = view;
                v.entityId = entityId;
                v.depthBias = data.settings.depthBias;
                v.slopeBias = data.settings.slopeBias;
                v.normalBias = data.settings.normalBias;
                v.texelSize = texelSize;
                v.lightSize = data.settings.lightSize;
                v.filterEnabled = globalSoftShadows;
                return v;
            };
            auto addViews = [&](std::vector<ShadowView>& dest, std::unordered_map<uint32_t, int32_t>& idx) {
                if (!idx.contains(entityId))
                    idx[entityId] = static_cast<int32_t>(dest.size());
                for (const auto& view : data.views)
                    dest.push_back(makeView(view));
            };

            switch (data.type)
            {
            case ShadowMapType::DirectionalCSM:
            case ShadowMapType::DirectionalClipmap: addViews(directionalShadowViews, dirIdx); break;
            case ShadowMapType::Spot2D:             addViews(spotShadowViews, spotIdx); break;
            case ShadowMapType::PointCube:
                if (!data.views.empty()) addViews(pointShadowViews, ptIdx); break;
            default: break;
            }
        }

        int32_t ptOff = static_cast<int32_t>(directionalShadowViews.size());
        int32_t spOff = ptOff + static_cast<int32_t>(pointShadowViews.size());
        for (const auto& [id, i] : dirIdx)  entityToShadowIndex[id] = i;
        for (const auto& [id, i] : ptIdx)   entityToShadowIndex[id] = ptOff + i;
        for (const auto& [id, i] : spotIdx) entityToShadowIndex[id] = spOff + i;
    }

    void ShadowSystem::classifyLightsForUpdate(
        std::vector<PointLightRef>& pointLights,
        std::vector<SpotLightRef>& spotLights,
        std::vector<DirLightRef>& directionalLights)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;
            auto entity = static_cast<entt::entity>(entityId);
            if (!registry.valid(entity) || !registry.all_of<components::WorldTransformComponent>(entity))
                continue;
            if (data.isStatic)
                ++lastCacheStats.totalStaticLights;
            const auto& wm = registry.get<components::WorldTransformComponent>(entity).worldMatrix;

            // Compute distance-based shadow priority for streaming
            float dist = glm::length(glm::vec3(wm[3]) - lastCameraPosition);
            data.shadowPriority = 1.0f / (1.0f + dist * 0.01f);
            if (data.isStatic) data.shadowPriority += 0.3f;

            // Apply per-light shadow overrides each frame
            if (registry.all_of<components::ShadowOverrideComponent>(entity))
            {
                const auto& ovr = registry.get<components::ShadowOverrideComponent>(entity);
                if (ovr.depthBias >= 0.0f) data.settings.depthBias = ovr.depthBias;
                if (ovr.slopeBias >= 0.0f) data.settings.slopeBias = ovr.slopeBias;
                if (ovr.normalBias >= 0.0f) data.settings.normalBias = ovr.normalBias;
            }

            if (data.type == ShadowMapType::PointCube)
            {
                float r = registry.all_of<components::PointLightComponent>(entity)
                    ? registry.get<components::PointLightComponent>(entity).radius : data.settings.farPlane;
                pointLights.push_back({&data, wm, r});
            }
            else if (data.type == ShadowMapType::Spot2D)
            {
                float oa = 45.0f, rng = 20.0f;
                if (registry.all_of<components::SpotLightComponent>(entity))
                { oa = registry.get<components::SpotLightComponent>(entity).outerAngle;
                  rng = registry.get<components::SpotLightComponent>(entity).range; }
                spotLights.push_back({&data, wm, oa, rng});
            }
            else if (data.isDirectionalType())
                directionalLights.push_back({&data, wm});
            for (auto& view : data.views) view.cached = false;
            ++lastCacheStats.renderedThisFrame;
        }
    }

    void ShadowSystem::updateShadowCacheAfterRender()
    {
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.renderedFrameCount < 10)
                ++data.renderedFrameCount;
        }
    }

    void ShadowSystem::beginFrame(const glm::mat4& cameraView,
                                  const glm::mat4& cameraProjection,
                                  float cameraNear, float cameraFar,
                                  const std::unordered_set<uint32_t>* visibleLightIds)
    {
        if (!shadowsEnabled) return;
        ++frameCounter;
        newPagesAllocatedThisFrame = 0;

        glm::vec3 camPos = -glm::vec3(cameraView[3]) * glm::mat3(cameraView);
        glm::vec3 camFwd = -glm::vec3(cameraView[0][2], cameraView[1][2], cameraView[2][2]);
        cameraMovedThisFrame = glm::distance(camPos, lastCameraPosition) > CAMERA_MOVE_EPSILON
                            || glm::distance(camFwd, lastCameraForward) > CAMERA_MOVE_EPSILON;
        lastCameraPosition = camPos;
        lastCameraForward = camFwd;

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();
        pageRenderList.clear();
        updateStaticFlags();
        lastCacheStats = {};

        std::vector<PointLightRef> ptLights;
        std::vector<SpotLightRef> spLights;
        std::vector<DirLightRef> dirLights;
        classifyLightsForUpdate(ptLights, spLights, dirLights);

        auto& jobs = threading::JobSystem::instance();
        auto f1 = jobs.submit([this, &ptLights]() {
            for (auto& r : ptLights)
                updatePointCubeShadowMatricesFromData(*r.data, r.worldMatrix, r.radius);
        }, threading::JobPriority::HIGH);
        auto f2 = jobs.submit([this, &spLights]() {
            for (auto& r : spLights)
                updateSpotShadowMatricesFromData(*r.data, r.worldMatrix, r.outerAngle, r.range);
        }, threading::JobPriority::HIGH);
        CameraContext camera{cameraView, cameraProjection, cameraNear, cameraFar};
        auto f3 = jobs.submit([this, &dirLights, camera]() {
            for (auto& r : dirLights) {
                if (r.data->type == ShadowMapType::DirectionalClipmap)
                    updateDirectionalClipmapMatricesFromData(*r.data, r.worldMatrix, camera);
                else
                    updateDirectionalCSMMatricesFromData(*r.data, r.worldMatrix, camera);
            }
        }, threading::JobPriority::HIGH);
        f1.get(); f2.get(); f3.get();

        updateShadowCacheAfterRender();
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

        auto axes = LightSpaceAxes::fromDirection(lightDirection);
        float lightSpaceZ = glm::dot(cameraWorldPos, axes.lightDir);

        uint32_t levelCount = std::min(static_cast<uint32_t>(data.views.size()),
                                        data.settings.clipmapLevelCount);

        for (uint32_t i = 0; i < levelCount; ++i)
            updateClipmapLevelMatrices(data, i, cameraWorldPos, lightDirection, lightSpaceZ);
    }

    void ShadowSystem::updateClipmapLevelMatrices(LightShadowData& data, uint32_t level,
                                                    const glm::vec3& cameraWorldPos,
                                                    const glm::vec3& lightDirection,
                                                    float lightSpaceZ)
    {
        uint32_t pagesPerSide = (level < data.clipmapLevelPagesPerSide.size())
            ? data.clipmapLevelPagesPerSide[level] : 1;
        uint32_t levelResolution = pagesPerSide * vsm::PAGE_SIZE;

        auto texelSnapped = ClipmapShadowCalculator::computeClipmapLevel(
            level, data.settings.clipmapBaseExtent, cameraWorldPos, lightDirection, levelResolution);

        updateClipmapDirtyFlags(data, level, texelSnapped);

        glm::vec2 pageGridOrigin = (level < data.clipmapPageGridOrigin.size())
            ? data.clipmapPageGridOrigin[level] : glm::vec2(0.0f);
        auto pageGridLevel = ClipmapShadowCalculator::computeClipmapLevelStable(
            level, data.settings.clipmapBaseExtent, pageGridOrigin, lightSpaceZ,
            lightDirection, levelResolution);

        if (level < data.clipmapRenderVP.size())
            data.clipmapRenderVP[level] = pageGridLevel.viewProjMatrix;

        // Zero UV offset — use same VP for both lookup and rendering
        if (level < data.clipmapUVOffset.size())
            data.clipmapUVOffset[level] = glm::vec2(0.0f);

        // Store page-grid VP (same as render VP) for GPU lookup
        auto& view = data.views[level];
        view.viewMatrix = pageGridLevel.viewMatrix;
        view.projectionMatrix = pageGridLevel.projMatrix;
        view.viewProjectionMatrix = pageGridLevel.viewProjMatrix;
        view.nearPlane = texelSnapped.nearDistance;
        view.farPlane = texelSnapped.farDistance;
        view.lightDirection = glm::vec4(lightDirection, 0.0f);
        view.cascadeIndex = static_cast<uint16_t>(level);
        view.texelSize = texelSnapped.texelSize;

        float biasScale = 1.0f + static_cast<float>(level) * 0.3f;
        view.depthBias = data.settings.depthBias * biasScale;
        view.slopeBias = data.settings.slopeBias * biasScale;
        view.normalBias = data.settings.normalBias * biasScale;
    }

    void ShadowSystem::updateClipmapDirtyFlags(LightShadowData& data, uint32_t level,
                                                const ClipmapLevelData& levelData)
    {
        if (level >= data.clipmapLastSnapPositions.size())
            return;

        if (level >= data.clipmapLevelPageOffsets.size() || level >= data.clipmapLevelPagesPerSide.size())
            return;

        if (level >= data.clipmapScrollOffset.size() || level >= data.clipmapPageGridOrigin.size())
            return;

        if (level >= data.clipmapLevelInitialized.size())
            return;

        uint32_t pps = data.clipmapLevelPagesPerSide[level];
        uint32_t basePageIdx = data.clipmapLevelPageOffsets[level];

        // Compute page-grid shift (how many whole pages the origin moved)
        auto pgUpdate = ClipmapShadowCalculator::computePageGridShift(
            levelData, pps, data.clipmapPageGridOrigin[level]);

        bool isFirstFrame = !data.clipmapLevelInitialized[level];
        data.clipmapLevelInitialized[level] = true;
        data.clipmapPageGridOrigin[level] = pgUpdate.newPageGridOrigin;
        data.clipmapLastSnapPositions[level] = levelData.snapPosition;

        if (pgUpdate.pageShift.x == 0 && pgUpdate.pageShift.y == 0 && !isFirstFrame)
            return; // No page boundary crossed — all cached pages remain valid

        // Any page-grid shift invalidates ALL pages in this level because the
        // lookup VP matches the render VP — stale pages have depth from old VP
        for (uint32_t p = 0; p < pps * pps; ++p)
        {
            uint32_t pageIdx = basePageIdx + p;
            if (pageIdx < data.vsmPageDirty.size())
                data.vsmPageDirty[pageIdx] = true;
        }

        if (pgUpdate.fullInvalidation || isFirstFrame)
        {
            data.clipmapScrollOffset[level] = glm::ivec2(0);
            return;
        }

        glm::ivec2& scroll = data.clipmapScrollOffset[level];
        int ipps = static_cast<int>(pps);
        scroll.x = ((scroll.x + pgUpdate.pageShift.x) % ipps + ipps) % ipps;
        scroll.y = ((scroll.y + pgUpdate.pageShift.y) % ipps + ipps) % ipps;

        markExposedScrollPages(data.vsmPageDirty, basePageIdx, pps, scroll, pgUpdate.pageShift);
    }

    void ShadowSystem::markExposedScrollPages(std::vector<bool>& pageDirty,
                                                uint32_t basePageIdx, uint32_t pps,
                                                const glm::ivec2& scroll,
                                                const glm::ivec2& pageShift)
    {
        int ipps = static_cast<int>(pps);

        for (int col = 0; col < std::abs(pageShift.x); ++col)
        {
            int vx = (pageShift.x > 0)
                ? ((scroll.x - 1 - col) % ipps + ipps) % ipps
                : (scroll.x + col) % ipps;
            for (uint32_t vy = 0; vy < pps; ++vy)
            {
                uint32_t pageIdx = basePageIdx + vy * pps + static_cast<uint32_t>(vx);
                if (pageIdx < pageDirty.size())
                    pageDirty[pageIdx] = true;
            }
        }

        for (int row = 0; row < std::abs(pageShift.y); ++row)
        {
            int vy = (pageShift.y > 0)
                ? ((scroll.y - 1 - row) % ipps + ipps) % ipps
                : (scroll.y + row) % ipps;
            for (uint32_t vx = 0; vx < pps; ++vx)
            {
                uint32_t pageIdx = basePageIdx + static_cast<uint32_t>(vy) * pps + vx;
                if (pageIdx < pageDirty.size())
                    pageDirty[pageIdx] = true;
            }
        }
    }
}
