#include "LightBakeAdapter.hpp"
#include "../../utilities/components/CoreComponents.hpp"
#include "../../utilities/components/LightTextComponents.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/config/Config.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/lightbake/LightBakeEvents.hpp"
#include "../../services/events/terrain/TerrainEvents.hpp"
#include "../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../services/events/scene/EntityTransformEvents.hpp"
#include "../../services/data/EntityConversion.hpp"
#include "../../utilities/components/WaterComponents.hpp"
#include "print/Log.hpp"
#include <chrono>
#include <cmath>

namespace core
{
    LightBakeAdapter::LightBakeAdapter()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        sceneClearedToken = dispatcher.subscribe<::events::scene::SceneClearedNotification>(
            [this](const ::events::scene::SceneClearedNotification&)
            {
                terrainLightmapInfos.clear();
                lastLightmapPath.clear();
                lastTerrainTileInfos.clear();

                {
                    std::lock_guard<std::mutex> lock(resultMutex);
                    lastResult = {};
                }

                auto& d = ::events::EventDispatcher::instance();
                services::events::lightbake::LightmapClearedNotification notif;
                d.publish(notif);
            });

        sceneLoadedToken = dispatcher.subscribe<::events::scene::SceneLoadedNotification>(
            [this](const ::events::scene::SceneLoadedNotification&)
            {
                if (!lastLightmapPath.empty())
                {
                    loadLightmap(lastLightmapPath, lastTexelsPerUnit);
                    return;
                }

                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::LightmapComponent>();
                for (auto entity : view)
                {
                    const auto& lm = view.get<components::LightmapComponent>(entity);
                    if (!lm.lightmapPath.empty())
                    {
                        loadLightmap(lm.lightmapPath, lm.texelsPerUnit);
                        return;
                    }
                }
            });
    }

    LightBakeAdapter::~LightBakeAdapter() noexcept
    {
        if (baking.load())
        {
            baker.cancel();
            if (bakeFuture.valid())
            {
                bakeFuture.wait();
            }
        }
        auto& dispatcher = ::events::EventDispatcher::instance();
        dispatcher.unsubscribe(sceneClearedToken);
        dispatcher.unsubscribe(sceneLoadedToken);
    }

    void LightBakeAdapter::startBake(const services::LightBakeConfig& config)
    {
        if (baking.load())
        {
            vfLogWarning("[LightBake] Bake already in progress");
            return;
        }

        baking.store(true);
        progress.store(0.0f);

        bakeFuture = std::async(std::launch::async, &LightBakeAdapter::runBake, this, config);
    }

    void LightBakeAdapter::cancelBake()
    {
        if (baking.load())
        {
            baker.cancel();
        }
    }

    float LightBakeAdapter::getBakeProgress() const
    {
        return progress.load();
    }

    bool LightBakeAdapter::isBaking() const
    {
        return baking.load();
    }

    services::LightBakeResult LightBakeAdapter::getResult() const
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        return lastResult;
    }

    lightbake::BakeLightSet LightBakeAdapter::collectLightsFromScene() const
    {
        lightbake::BakeLightSet lights;
        collectDirectionalLights(lights);
        collectPointLights(lights);
        collectSpotLights(lights);
        return lights;
    }

    void LightBakeAdapter::collectDirectionalLights(lightbake::BakeLightSet& lights) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent,
                                   components::TransformComponent,
                                   components::WorldTransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic) continue;

            const auto& light = view.get<components::DirectionalLightComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            lightbake::BakeDirectionalLight bakeLight;
            // Direction must match GPU forward rendering: forward = -Z axis
            bakeLight.direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            bakeLight.color = light.color;
            bakeLight.intensity = light.intensity;
            lights.directionalLights.push_back(bakeLight);
        }
    }

    void LightBakeAdapter::collectPointLights(lightbake::BakeLightSet& lights) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PointLightComponent,
                                   components::TransformComponent,
                                   components::WorldTransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic) continue;

            const auto& light = view.get<components::PointLightComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            lightbake::BakePointLight bakeLight;
            bakeLight.position = glm::vec3(worldTransform.worldMatrix[3]);
            bakeLight.color = light.color;
            bakeLight.intensity = light.intensity;
            bakeLight.radius = light.radius;
            lights.pointLights.push_back(bakeLight);
        }
    }

    void LightBakeAdapter::collectSpotLights(lightbake::BakeLightSet& lights) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SpotLightComponent,
                                   components::TransformComponent,
                                   components::WorldTransformComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic) continue;

            const auto& light = view.get<components::SpotLightComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            lightbake::BakeSpotLight bakeLight;
            bakeLight.position = glm::vec3(worldTransform.worldMatrix[3]);
            // Direction must match GPU forward rendering: forward = -Z axis
            bakeLight.direction = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            bakeLight.color = light.color;
            bakeLight.intensity = light.intensity;
            bakeLight.range = light.range;
            bakeLight.cosInnerAngle = std::cos(glm::radians(light.innerAngle));
            bakeLight.cosOuterAngle = std::cos(glm::radians(light.outerAngle));
            lights.spotLights.push_back(bakeLight);
        }
    }

    lightbake::TerrainBakeGeometry LightBakeAdapter::collectTerrainGeometry()
    {
        lightbake::TerrainBakeGeometry result;

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::terrain::GetTerrainBakeGeometryQuery query;
        auto terrainResult = dispatcher.query(query);

        if (!terrainResult.vertices.empty())
        {
            result.vertices = std::move(terrainResult.vertices);
            result.triangles = std::move(terrainResult.triangles);

            for (const auto& tileInfo : terrainResult.tileInfos)
            {
                lightbake::TerrainTileBakeInfo bakeInfo;
                bakeInfo.coordX = tileInfo.coordX;
                bakeInfo.coordZ = tileInfo.coordZ;
                bakeInfo.worldOrigin = tileInfo.worldOrigin;
                bakeInfo.tileSize = tileInfo.tileSize;
                bakeInfo.firstVertexIndex = tileInfo.firstVertexIndex;
                bakeInfo.vertexCount = tileInfo.vertexCount;
                bakeInfo.firstTriangleIndex = tileInfo.firstTriangleIndex;
                bakeInfo.triangleCount = tileInfo.triangleCount;
                result.tileInfos.push_back(bakeInfo);
            }

            lastTerrainTileInfos = result.tileInfos;
        }
        else
        {
            vfLogWarning("[LightBake] collectTerrainGeometry: NO terrain vertices returned!");
            lastTerrainTileInfos.clear();
        }

        return result;
    }

    std::vector<lightbake::WaterBakeTile> LightBakeAdapter::collectWaterTiles() const
    {
        std::vector<lightbake::WaterBakeTile> result;
        auto& registry = scene::EntityRegistry::getRegistry();

        float worldTileSize = 32.0f;
        auto waterView = registry.view<components::WaterComponent>();
        for (auto entity : waterView)
        {
            const auto& waterComp = waterView.get<components::WaterComponent>(entity);
            worldTileSize = waterComp.worldTileSize;
            break; // Use first water entity's config
        }

        auto tileView = registry.view<components::WaterTileComponent>();
        for (auto entity : tileView)
        {
            const auto& tileComp = tileView.get<components::WaterTileComponent>(entity);

            lightbake::WaterBakeTile tile;
            tile.worldOrigin = glm::vec3(
                static_cast<float>(tileComp.tileX) * worldTileSize,
                tileComp.waterHeight,
                static_cast<float>(tileComp.tileZ) * worldTileSize
            );
            tile.waterHeight = tileComp.waterHeight;
            tile.worldTileSize = worldTileSize;
            tile.subdivisions = 8; // Low res for shadow casting only
            result.push_back(tile);
        }

        return result;
    }

    void LightBakeAdapter::storeLightmapOnRoot(const std::string& path, float texelsPerUnit)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto rootHandle = ::events::EventDispatcher::instance().query(::events::scene::GetRootEntityQuery{});
        if (rootHandle.isValid())
        {
            auto rootEntity = services::internal::fromHandle(rootHandle);
            auto& lm = registry.emplace_or_replace<components::LightmapComponent>(rootEntity);
            lm.lightmapPath = path;
            lm.texelsPerUnit = texelsPerUnit;
        }
    }

    void LightBakeAdapter::assignLightmapComponents(
        const resource::LightmapData& lightmapData,
        const std::string& outputPath, float texelsPerUnit)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        terrainLightmapInfos.clear();

        uint32_t entityCount = 0;
        uint32_t terrainTileCount = 0;

        for (const auto& region : lightmapData.entityRegions)
        {
            if (region.entityId >= lightbake::TERRAIN_ENTITY_BASE)
            {
                uint32_t tileIndex = region.entityId - lightbake::TERRAIN_ENTITY_BASE;
                if (tileIndex < static_cast<uint32_t>(lastTerrainTileInfos.size()))
                {
                    const auto& tileInfo = lastTerrainTileInfos[tileIndex];
                    services::TerrainLightmapTileInfo info;
                    info.coordX = tileInfo.coordX;
                    info.coordZ = tileInfo.coordZ;
                    info.scaleOffset = region.scaleOffset;
                    info.lightmapPath = outputPath;
                    terrainLightmapInfos.push_back(info);
                    terrainTileCount++;
                }
                continue;
            }

            auto entity = static_cast<entt::entity>(region.entityId);
            if (!registry.valid(entity))
            {
                continue;
            }

            auto& lm = registry.emplace_or_replace<components::LightmapComponent>(entity);
            lm.lightmapPath = outputPath;
            lm.texelsPerUnit = texelsPerUnit;
            lm.atlasScaleOffset = region.scaleOffset;
            entityCount++;
        }
    }

    std::vector<services::TerrainLightmapTileInfo> LightBakeAdapter::getTerrainLightmapData() const
    {
        return terrainLightmapInfos;
    }

    void LightBakeAdapter::publishBakeFailure(const std::string& errorMessage)
    {
        baking.store(false);
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (baker.wasCancelled())
        {
            services::events::lightbake::BakeCancelledNotification notif;
            dispatcher.publish(notif);
        }
        else
        {
            services::events::lightbake::BakeFailedNotification notif;
            notif.errorMessage = errorMessage;
            dispatcher.publish(notif);
        }
    }

    void LightBakeAdapter::finalizeBake(services::LightBakeResult& result,
                                         const resource::LightmapData& lightmapData,
                                         const std::string& outputPath, float texelsPerUnit,
                                         std::chrono::high_resolution_clock::time_point startTime)
    {
        assignLightmapComponents(lightmapData, outputPath, texelsPerUnit);

        storeLightmapOnRoot(outputPath, texelsPerUnit);

        lastLightmapPath = outputPath;
        lastTexelsPerUnit = texelsPerUnit;

        auto endTime = std::chrono::high_resolution_clock::now();
        float elapsedSeconds = std::chrono::duration<float>(endTime - startTime).count();

        result.success = true;
        result.lightmapPath = outputPath;
        result.atlasWidth = lightmapData.width;
        result.atlasHeight = lightmapData.height;
        result.bakeTimeSeconds = elapsedSeconds;

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastResult = result;
        }

        progress.store(1.0f);
        baking.store(false);

        auto& dispatcher = ::events::EventDispatcher::instance();
        services::events::lightbake::BakeCompletedNotification notif;
        notif.result = result;
        dispatcher.publish(notif);

        vfLogInfo("[LightBake] Bake complete: {}x{} atlas, {} lights, {:.2f}s",
                     result.atlasWidth, result.atlasHeight, result.bakedLightCount, result.bakeTimeSeconds);
    }

    void LightBakeAdapter::runBake(const services::LightBakeConfig& config)
    {
        auto startTime = std::chrono::high_resolution_clock::now();
        baker.reset();

        services::LightBakeResult result;

        // Step 1: Build scene mesh BVH (0% - 20%)
        auto terrainGeometry = collectTerrainGeometry();
        auto waterTiles = collectWaterTiles();

        lightbake::BakeSceneMesh sceneMesh;
        bool sceneBuilt = sceneMesh.buildFromScene(
            terrainGeometry, waterTiles,
            [this](float p) { progress.store(p * 0.2f); }
        );
        if (!sceneBuilt || baker.wasCancelled())
        {
            publishBakeFailure("Failed to build scene mesh BVH");
            return;
        }

        // Step 2: Generate lightmap atlas (20% - 30%)
        progress.store(0.2f);

        lightbake::LightmapConfig lmConfig;
        lmConfig.texelsPerUnit = config.texelsPerUnit;
        lmConfig.maxAtlasSize = config.maxAtlasSize;

        lightbake::LightmapAtlas atlas;
        if (!atlas.build(sceneMesh.getBVH(), lmConfig))
        {
            publishBakeFailure("Failed to build lightmap atlas");
            return;
        }
        progress.store(0.3f);

        // Step 3: Collect lights (30%)
        auto lights = collectLightsFromScene();
        if (lights.empty())
        {
            publishBakeFailure("No static lights found in scene");
            return;
        }

        result.bakedLightCount = static_cast<uint32_t>(
            lights.directionalLights.size() + lights.pointLights.size() + lights.spotLights.size());

        // Step 4: Run baker (30% - 95%)
        auto& lightmapData = atlas.getLightmapData();

        bool bakeSuccess = baker.bake(sceneMesh, atlas, lights, lightmapData,
            [this](float p) { progress.store(0.3f + p * 0.65f); }
        );
        if (!bakeSuccess)
        {
            publishBakeFailure("Bake failed");
            return;
        }

        progress.store(0.95f);
        std::string outputPath = config.outputPath;
        if (outputPath.empty())
        {
            outputPath = "lightmap." + FileExtension::lightmap;
        }

        if (!lightbake::LightmapAtlas::save(lightmapData, outputPath))
        {
            publishBakeFailure("Failed to save lightmap to: " + outputPath);
            return;
        }

        finalizeBake(result, lightmapData, outputPath, config.texelsPerUnit, startTime);
    }

    void LightBakeAdapter::clearLightmap()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        auto view = registry.view<components::LightmapComponent>();
        for (auto entity : view)
        {
            registry.remove<components::LightmapComponent>(entity);
        }

        terrainLightmapInfos.clear();
        lastLightmapPath.clear();

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastResult = {};
        }


        auto& dispatcher = ::events::EventDispatcher::instance();
        services::events::lightbake::LightmapClearedNotification notif;
        dispatcher.publish(notif);
    }

    bool LightBakeAdapter::loadLightmap(const std::string& path, float texelsPerUnit)
    {
        auto lightmapData = lightbake::LightmapAtlas::load(path);

        if (lightmapData.width == 0 || lightmapData.height == 0 || lightmapData.texels.empty())
        {
            vfLogWarning("[LightBake] Failed to load lightmap from: {}", path);
            return false;
        }

        bool hasTerrainRegions = false;
        for (const auto& region : lightmapData.entityRegions)
        {
            if (region.entityId >= lightbake::TERRAIN_ENTITY_BASE)
            {
                hasTerrainRegions = true;
                break;
            }
        }
        if (hasTerrainRegions && lastTerrainTileInfos.empty())
        {
            collectTerrainGeometry();
        }

        assignLightmapComponents(lightmapData, path, texelsPerUnit);

        storeLightmapOnRoot(path, texelsPerUnit);

        lastLightmapPath = path;
        lastTexelsPerUnit = texelsPerUnit;

        vfLogInfo("[LightBake] Loaded lightmap from: {} ({}x{}, {} entities)",
                     path, lightmapData.width, lightmapData.height, lightmapData.entityRegions.size());

        auto& dispatcher = ::events::EventDispatcher::instance();
        services::events::lightbake::LightmapLoadedNotification notif;
        notif.lightmapPath = path;
        dispatcher.publish(notif);

        return true;
    }
}
