#include "ShadowSystem.hpp"
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
            view.texelSize = 1.0f / static_cast<float>(data.settings.resolution);
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

    void ShadowSystem::collectShadowViewsForGPU(const std::unordered_set<uint32_t>* visibleLightIds)
    {
        std::unordered_map<uint32_t, int32_t> ptIdx, spotIdx;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;
            if (visibleLightIds && !visibleLightIds->contains(entityId))
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
            case ShadowMapType::Spot2D:             addViews(spotShadowViews, spotIdx); break;
            case ShadowMapType::PointCube:
                if (!data.views.empty()) addViews(pointShadowViews, ptIdx); break;
            default: break;
            }
        }

        int32_t spOff = static_cast<int32_t>(pointShadowViews.size());
        for (const auto& [id, i] : ptIdx)   entityToShadowIndex[id] = i;
        for (const auto& [id, i] : spotIdx) entityToShadowIndex[id] = spOff + i;
    }

    void ShadowSystem::classifyLightsForUpdate(
        std::vector<PointLightRef>& pointLights,
        std::vector<SpotLightRef>& spotLights)
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

            // Distance culling: skip shadows for lights beyond max shadow distance
            if (dist > data.settings.maxShadowDistance)
                continue;

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
        buildEvictionHeap();

        glm::vec3 camPos = -glm::vec3(cameraView[3]) * glm::mat3(cameraView);
        lastCameraPosition = camPos;

        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();
        pageRenderList.clear();
        updateStaticFlags();
        lastCacheStats = {};

        std::vector<PointLightRef> ptLights;
        std::vector<SpotLightRef> spLights;
        classifyLightsForUpdate(ptLights, spLights);

        auto& jobs = threading::JobSystem::instance();
        auto f1 = jobs.submit([this, &ptLights]() {
            for (auto& r : ptLights)
                updatePointCubeShadowMatricesFromData(*r.data, r.worldMatrix, r.radius);
        }, threading::JobPriority::HIGH);
        auto f2 = jobs.submit([this, &spLights]() {
            for (auto& r : spLights)
                updateSpotShadowMatricesFromData(*r.data, r.worldMatrix, r.outerAngle, r.range);
        }, threading::JobPriority::HIGH);
        f1.get(); f2.get();

        updateShadowCacheAfterRender();
        collectShadowViewsForGPU(visibleLightIds);
        applyFeedbackAllocations();
        determineDynamicPages();
        buildPageRenderList();
    }

}
