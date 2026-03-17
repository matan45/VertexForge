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
                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.lightSize = data.settings.lightSize;
                        viewCopy.filterEnabled = globalSoftShadows;

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

                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.lightSize = data.settings.lightSize;
                        viewCopy.filterEnabled = globalSoftShadows;

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
                        ShadowView viewCopy = view;
                        viewCopy.entityId = entityId;
                        viewCopy.depthBias = data.settings.depthBias;
                        viewCopy.slopeBias = data.settings.slopeBias;
                        viewCopy.normalBias = data.settings.normalBias;
                        viewCopy.texelSize = texelSize;
                        viewCopy.lightSize = data.settings.lightSize;
                        viewCopy.filterEnabled = globalSoftShadows;

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
                    viewCopy.lightSize = data.settings.lightSize;
                    viewCopy.filterEnabled = globalSoftShadows;
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

    void ShadowSystem::buildPageRenderList()
    {
        pageRenderList.clear();
        lastCacheStats.totalPages = 0;
        lastCacheStats.renderedPages = 0;
        lastCacheStats.cachedPages = 0;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
                continue; // Point lights use cubemaps, not VSM pages

            if (data.vsmPhysicalTiles.empty())
                continue;

            // Ensure dirty vector is sized
            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageDirty.size() != totalPages)
                data.vsmPageDirty.resize(totalPages, true);

            if (data.type == ShadowMapType::DirectionalCSM)
            {
                uint32_t pagesPerCascade = data.vsmPagesX;
                uint32_t cascadeCount = data.settings.cascadeCount;

                // CSM cascades are camera-dependent — always dirty when camera moves
                bool csmDirty = cameraMovedThisFrame;

                for (uint32_t cascade = 0; cascade < cascadeCount && cascade < data.views.size(); ++cascade)
                {
                    const auto& view = data.views[cascade];
                    if (view.cached)
                        continue;

                    for (uint32_t py = 0; py < pagesPerCascade; ++py)
                    {
                        for (uint32_t px = 0; px < pagesPerCascade; ++px)
                        {
                            uint32_t pageIdx = (cascade * pagesPerCascade + py) * pagesPerCascade + px;
                            if (pageIdx >= data.vsmPhysicalTiles.size())
                                continue;

                            uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
                            if (physTile == vsm::INVALID_TILE)
                                continue;

                            ++lastCacheStats.totalPages;

                            // Phase 3: skip clean pages (cached) unless CSM needs update
                            if (!csmDirty && !data.vsmPageDirty[pageIdx])
                            {
                                ++lastCacheStats.cachedPages;
                                continue;
                            }

                            glm::mat4 cropMatrix = vsm::computePageCropMatrix(px, py, pagesPerCascade, pagesPerCascade);
                            glm::mat4 cropVP = cropMatrix * view.viewProjectionMatrix;

                            PageRenderEntry entry;
                            entry.physicalTileIndex = physTile;
                            entry.cropViewProjection = cropVP;
                            entry.depthBias = view.depthBias;
                            entry.slopeBias = view.slopeBias;
                            entry.normalBias = view.normalBias;
                            pageRenderList.push_back(entry);
                            ++lastCacheStats.renderedPages;

                            // Mark page as clean after adding to render list
                            data.vsmPageDirty[pageIdx] = false;
                        }
                    }
                }
            }
            else
            {
                // Spot or Directional2D - single view
                if (data.views.empty())
                    continue;

                const auto& view = data.views[0];
                if (view.cached)
                    continue;

                for (uint32_t py = 0; py < data.vsmPagesY; ++py)
                {
                    for (uint32_t px = 0; px < data.vsmPagesX; ++px)
                    {
                        uint32_t pageIdx = py * data.vsmPagesX + px;
                        if (pageIdx >= data.vsmPhysicalTiles.size())
                            continue;

                        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
                        if (physTile == vsm::INVALID_TILE)
                            continue;

                        ++lastCacheStats.totalPages;

                        // Phase 3: skip clean pages for static spot/dir2D lights
                        if (!data.vsmPageDirty[pageIdx])
                        {
                            ++lastCacheStats.cachedPages;
                            continue;
                        }

                        glm::mat4 cropMatrix = vsm::computePageCropMatrix(px, py, data.vsmPagesX, data.vsmPagesY);
                        glm::mat4 cropVP = cropMatrix * view.viewProjectionMatrix;

                        PageRenderEntry entry;
                        entry.physicalTileIndex = physTile;
                        entry.cropViewProjection = cropVP;
                        entry.depthBias = view.depthBias;
                        entry.slopeBias = view.slopeBias;
                        entry.normalBias = view.normalBias;
                        pageRenderList.push_back(entry);
                        ++lastCacheStats.renderedPages;

                        // Mark page as clean after adding to render list
                        data.vsmPageDirty[pageIdx] = false;
                    }
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

        ++frameCounter;

        // Phase 3: Detect camera movement for CSM caching
        cameraMovedThisFrame = (cameraView != lastCameraView || cameraProjection != lastCameraProjection);
        lastCameraView = cameraView;
        lastCameraProjection = cameraProjection;

        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        entityToShadowIndex.clear();
        pageRenderList.clear();

        // Sync static flags from ECS
        updateStaticFlags();

        // Reset per-frame cache stats
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

            // Skip matrix computation for cached static point/spot lights
            if (data.isStatic && data.shadowCached && data.type != ShadowMapType::DirectionalCSM)
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
            else if (data.type == ShadowMapType::DirectionalCSM)
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

        // Apply feedback-driven page allocation (uses previous frame's results)
        applyFeedbackAllocations();

        buildPageRenderList();
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

        // Upload page table
        if (pageTable)
            pageTable->uploadToGPU(cmd);

        gpuDataManager->updateShadowTextureDescriptor(tilePool.get(), resourcePool.get(), lightShadowData);

        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd,
                                         const ShadowPassParams& params,
                                         const TerrainShadowPassParams* terrainParams)
    {
        if (!passRecorder)
            return;

        passRecorder->recordShadowPass(cmd, params, terrainParams,
            tilePool.get(), resourcePool.get(),
            shadowPassPipeline.get(), terrainShadowPipeline.get(),
            pageRenderList, lightShadowData, shadowsEnabled, poolFirstUse);

        if (shadowsEnabled)
            poolFirstUse = false;
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

        globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);

        // Update all lights' bias settings and cascade count
        for (auto& [entityId, data] : lightShadowData)
        {
            data.settings.depthBias = shadowSettings.shadowBias;
            data.settings.slopeBias = shadowSettings.slopeBias;
            data.settings.normalBias = shadowSettings.normalBias;

            if (data.type == ShadowMapType::DirectionalCSM &&
                data.settings.cascadeCount != shadowSettings.cascadeCount)
            {
                // Free and reallocate with new cascade count
                if (data.usesVSM())
                    freeVSMPages(data);

                data.settings.cascadeCount = shadowSettings.cascadeCount;
                data.views.resize(shadowSettings.cascadeCount);

                if (data.usesVSM())
                    allocateVSMPages(data);
            }

            data.settingsDirty = true;
        }

        poolFirstUse = true;
        needsUpdate = true;

        globalSoftShadows = shadowSettings.softShadows;
    }

    // ============================================================
    // Phase 3: Scene Change Notifications
    // ============================================================

    void ShadowSystem::notifyObjectMoved(uint32_t entityId, const glm::vec3& position, float radius)
    {
        // Mark pages dirty for all lights whose frustum overlaps the object's bounding sphere
        for (auto& [lightEntityId, data] : lightShadowData)
        {
            bool anyPageDirtied = false;

            if (data.usesVSM() && !data.vsmPhysicalTiles.empty())
            {
                if (data.type == ShadowMapType::DirectionalCSM)
                {
                    uint32_t pagesPerCascade = data.vsmPagesX;
                    for (uint32_t cascade = 0; cascade < data.settings.cascadeCount && cascade < data.views.size(); ++cascade)
                    {
                        const auto& view = data.views[cascade];
                        glm::vec4 lsPos = view.viewProjectionMatrix * glm::vec4(position, 1.0f);
                        if (lsPos.w <= 0.0f) continue;

                        glm::vec3 ndc = glm::vec3(lsPos) / lsPos.w;
                        glm::vec2 uv = glm::vec2(ndc) * 0.5f + 0.5f;

                        float uvRadius = radius / (2.0f * lsPos.w) * static_cast<float>(pagesPerCascade);
                        int minPX = std::max(0, static_cast<int>((uv.x - uvRadius) * pagesPerCascade));
                        int maxPX = std::min(static_cast<int>(pagesPerCascade) - 1, static_cast<int>((uv.x + uvRadius) * pagesPerCascade));
                        int minPY = std::max(0, static_cast<int>((uv.y - uvRadius) * pagesPerCascade));
                        int maxPY = std::min(static_cast<int>(pagesPerCascade) - 1, static_cast<int>((uv.y + uvRadius) * pagesPerCascade));

                        for (int py = minPY; py <= maxPY; ++py)
                        {
                            for (int px = minPX; px <= maxPX; ++px)
                            {
                                uint32_t pageIdx = (cascade * pagesPerCascade + py) * pagesPerCascade + px;
                                if (pageIdx < data.vsmPageDirty.size())
                                {
                                    data.vsmPageDirty[pageIdx] = true;
                                    anyPageDirtied = true;
                                }
                            }
                        }
                    }
                }
                else
                {
                    if (!data.views.empty())
                    {
                        const auto& view = data.views[0];
                        glm::vec4 lsPos = view.viewProjectionMatrix * glm::vec4(position, 1.0f);
                        if (lsPos.w > 0.0f)
                        {
                            glm::vec3 ndc = glm::vec3(lsPos) / lsPos.w;
                            if (glm::abs(ndc.x) <= 1.5f && glm::abs(ndc.y) <= 1.5f)
                            {
                                for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
                                    data.vsmPageDirty[i] = true;
                                anyPageDirtied = true;
                            }
                        }
                    }
                }
            }
            else if (data.type == ShadowMapType::PointCube)
            {
                // Point light cubemap: check if object is within light radius
                if (!data.views.empty())
                {
                    glm::vec3 lightPos = glm::vec3(data.views[0].lightPosition);
                    float dist = glm::distance(lightPos, position);
                    if (dist < data.settings.farPlane + radius)
                        anyPageDirtied = true;
                }
            }

            // Reset view-level cache so dirty pages actually get re-rendered
            if (anyPageDirtied)
            {
                data.shadowCached = false;
                data.renderedFrameCount = 0;
                for (auto& view : data.views)
                    view.cached = false;
                needsUpdate = true;
            }
        }
    }

    void ShadowSystem::notifySceneChanged()
    {
        // Mark ALL pages dirty across all lights and reset view-level cache
        for (auto& [entityId, data] : lightShadowData)
        {
            // Reset page-level dirty flags
            for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
                data.vsmPageDirty[i] = true;

            // Reset view-level cache so pages actually get re-rendered
            data.shadowCached = false;
            data.renderedFrameCount = 0;
            for (auto& view : data.views)
                view.cached = false;
        }
        needsUpdate = true;
    }

    // ============================================================
    // GPU Feedback (Phase 2)
    // ============================================================

    void ShadowSystem::dispatchFeedback(vk::CommandBuffer cmd, vk::ImageView depthView,
                                         const glm::mat4& invViewProjection,
                                         uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!feedbackEnabled || !feedbackPipeline || !feedbackPipeline->isInitialized() || !gpuDataManager)
            return;

        // Clear feedback buffer first
        feedbackPipeline->clearFeedbackBuffer(cmd);

        // Count VSM lights (exclude point lights)
        uint32_t vsmLightCount = static_cast<uint32_t>(
            directionalShadowViews.size() + spotShadowViews.size()
        );

        // Total shadow views = all views uploaded to GPU
        uint32_t totalViews = static_cast<uint32_t>(
            directionalShadowViews.size() + pointShadowViews.size() + spotShadowViews.size()
        );

        if (totalViews == 0)
            return;

        // The shadow data buffer contains all views in order [dir][point][spot]
        // The feedback shader iterates all and skips point lights (lightType == 2)
        vk::DeviceSize shadowDataSize = sizeof(vsm::GPUVSMLight) * totalViews;

        feedbackPipeline->dispatch(cmd, depthView,
            gpuDataManager->getShadowDataBuffer(), shadowDataSize,
            totalViews, invViewProjection, screenWidth, screenHeight);
    }

    void ShadowSystem::copyFeedbackToStaging(vk::CommandBuffer cmd)
    {
        if (!feedbackPipeline || !feedbackPipeline->isInitialized())
            return;

        feedbackPipeline->copyResultsToStaging(cmd);
    }

    void ShadowSystem::markFeedbackReady()
    {
        if (feedbackPipeline)
            feedbackPipeline->markResultsReady();
    }

    void ShadowSystem::readBackFeedback()
    {
        if (!feedbackPipeline || !feedbackEnabled)
            return;

        if (feedbackPipeline->getReadbackState() != FeedbackReadbackState::Ready)
            return;

        // Read back the used portion of the feedback buffer
        uint32_t usedEntries = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.usesVSM())
            {
                uint32_t end = data.vsmPageTableOffset + data.vsmPagesX * data.vsmPagesY;
                if (end > usedEntries)
                    usedEntries = end;
            }
        }

        if (usedEntries == 0)
            return;

        prevFrameFeedback = feedbackPipeline->readbackResults(usedEntries);
        feedbackHasResults = !prevFrameFeedback.empty();
    }

    void ShadowSystem::applyFeedbackAllocations()
    {
        if (!feedbackEnabled || !feedbackHasResults || prevFrameFeedback.empty())
            return;

        // Don't evict pages during warmup period (allow feedback to stabilize)
        static constexpr uint32_t WARMUP_FRAMES = 120; // ~2 seconds at 60fps
        bool allowEviction = frameCounter > WARMUP_FRAMES;

        if (!tilePool || !pageTable)
            return;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.usesVSM())
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (totalPages == 0)
                continue;

            // Ensure tracking vectors are sized
            if (data.vsmPhysicalTiles.size() != totalPages)
                data.vsmPhysicalTiles.resize(totalPages, vsm::INVALID_TILE);
            if (data.vsmPageLastUsedFrame.size() != totalPages)
                data.vsmPageLastUsedFrame.resize(totalPages, 0);

            for (uint32_t i = 0; i < totalPages; ++i)
            {
                uint32_t feedbackIdx = data.vsmPageTableOffset + i;
                if (feedbackIdx >= prevFrameFeedback.size())
                    continue;

                bool pageNeeded = prevFrameFeedback[feedbackIdx] > 0;

                if (pageNeeded)
                {
                    data.vsmPageLastUsedFrame[i] = frameCounter;

                    // Allocate physical tile if not already allocated
                    if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                    {
                        uint32_t tile = tilePool->allocateTile();
                        if (tile != vsm::INVALID_TILE)
                        {
                            data.vsmPhysicalTiles[i] = tile;
                            uint32_t px = i % data.vsmPagesX;
                            uint32_t py = i / data.vsmPagesX;
                            pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, tile);
                        }
                    }
                }
                else
                {
                    // Page not needed - check eviction threshold (only after warmup)
                    if (allowEviction &&
                        data.vsmPhysicalTiles[i] != vsm::INVALID_TILE &&
                        frameCounter - data.vsmPageLastUsedFrame[i] > EVICTION_THRESHOLD)
                    {
                        tilePool->freeTile(data.vsmPhysicalTiles[i]);
                        uint32_t px = i % data.vsmPagesX;
                        uint32_t py = i / data.vsmPagesX;
                        pageTable->unmapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX);
                        data.vsmPhysicalTiles[i] = vsm::INVALID_TILE;
                    }
                }
            }
        }
    }
}
