#include "LightBakeAdapter.hpp"
#include "../../utilities/components/CoreComponents.hpp"
#include "../../utilities/components/LightTextComponents.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/config/Config.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/LightBakeEvents.hpp"
#include "../../services/events/TerrainEvents.hpp"
#include "../../utilities/components/WaterComponents.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

namespace core
{
    LightBakeAdapter::~LightBakeAdapter() noexcept
    {
        if (baking_.load())
        {
            baker_.cancel();
            if (bakeFuture_.valid())
            {
                bakeFuture_.wait();
            }
        }
    }

    void LightBakeAdapter::startBake(const services::LightBakeConfig& config)
    {
        if (baking_.load())
        {
            spdlog::warn("[LightBake] Bake already in progress");
            return;
        }

        // Launch bake on background thread
        baking_.store(true);
        progress_.store(0.0f);

        bakeFuture_ = std::async(std::launch::async, &LightBakeAdapter::runBake, this, config);
    }

    void LightBakeAdapter::cancelBake()
    {
        if (baking_.load())
        {
            baker_.cancel();
        }
    }

    float LightBakeAdapter::getBakeProgress() const
    {
        return progress_.load();
    }

    bool LightBakeAdapter::isBaking() const
    {
        return baking_.load();
    }

    services::LightBakeResult LightBakeAdapter::getResult() const
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        return lastResult_;
    }

    lightbake::BakeLightSet LightBakeAdapter::collectLightsFromScene() const
    {
        lightbake::BakeLightSet lights;
        auto& registry = scene::EntityRegistry::getRegistry();

        // Collect directional lights from static entities
        {
            auto view = registry.view<components::DirectionalLightComponent,
                                       components::TransformComponent>();
            for (auto entity : view)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                if (!transform.isStatic) continue;

                const auto& light = view.get<components::DirectionalLightComponent>(entity);

                lightbake::BakeDirectionalLight bakeLight;
                // Direction from rotation
                glm::mat4 rotMat = transform.getMatrix();
                bakeLight.direction = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
                bakeLight.color = light.color;
                bakeLight.intensity = light.intensity;
                lights.directionalLights.push_back(bakeLight);
            }
        }

        // Collect point lights from static entities
        {
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

        // Collect spot lights from static entities
        {
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
                // Direction from world matrix forward vector
                glm::mat4 rotMat = transform.getMatrix();
                bakeLight.direction = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f)));
                bakeLight.color = light.color;
                bakeLight.intensity = light.intensity;
                bakeLight.range = light.range;
                bakeLight.cosInnerAngle = std::cos(glm::radians(light.innerAngle));
                bakeLight.cosOuterAngle = std::cos(glm::radians(light.outerAngle));
                lights.spotLights.push_back(bakeLight);
            }
        }

        return lights;
    }

    lightbake::TerrainBakeGeometry LightBakeAdapter::collectTerrainGeometry() const
    {
        lightbake::TerrainBakeGeometry result;

        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::terrain::GetTerrainGeometryQuery query;
        auto terrainResult = dispatcher.query(query);

        if (!terrainResult.vertices.empty())
        {
            result.vertices = std::move(terrainResult.vertices);
            result.triangles = std::move(terrainResult.triangles);
            spdlog::info("[LightBake] Collected terrain geometry: {} vertices, {} triangles",
                         result.vertices.size() / 3, result.triangles.size() / 3);
        }

        return result;
    }

    std::vector<lightbake::WaterBakeTile> LightBakeAdapter::collectWaterTiles() const
    {
        std::vector<lightbake::WaterBakeTile> result;
        auto& registry = scene::EntityRegistry::getRegistry();

        // Find water config from WaterComponent entities
        float worldTileSize = 32.0f;
        auto waterView = registry.view<components::WaterComponent>();
        for (auto entity : waterView)
        {
            const auto& waterComp = waterView.get<components::WaterComponent>(entity);
            worldTileSize = waterComp.worldTileSize;
            break; // Use first water entity's config
        }

        // Collect all water tiles
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

        if (!result.empty())
        {
            spdlog::info("[LightBake] Collected {} water tiles", result.size());
        }

        return result;
    }

    void LightBakeAdapter::runBake(const services::LightBakeConfig& config)
    {
        auto startTime = std::chrono::high_resolution_clock::now();
        baker_.reset();

        services::LightBakeResult result;

        // Step 1: Build scene mesh BVH (0% - 20%)
        spdlog::info("[LightBake] Step 1/4: Building scene mesh BVH...");

        auto terrainGeometry = collectTerrainGeometry();
        auto waterTiles = collectWaterTiles();

        lightbake::BakeSceneMesh sceneMesh;
        bool sceneBuilt = sceneMesh.buildFromScene(
            terrainGeometry, waterTiles,
            [this](float p) { progress_.store(p * 0.2f); }
        );

        if (!sceneBuilt || baker_.wasCancelled())
        {
            baking_.store(false);
            if (!baker_.wasCancelled())
            {
                auto& dispatcher = ::events::EventDispatcher::instance();
                services::events::lightbake::BakeFailedNotification notif;
                notif.errorMessage = "Failed to build scene mesh BVH";
                dispatcher.publish(notif);
            }
            return;
        }

        // Step 2: Generate lightmap atlas (20% - 30%)
        spdlog::info("[LightBake] Step 2/4: Generating lightmap UV atlas...");
        progress_.store(0.2f);

        lightbake::LightmapConfig lmConfig;
        lmConfig.texelsPerUnit = config.texelsPerUnit;
        lmConfig.maxAtlasSize = config.maxAtlasSize;

        lightbake::LightmapAtlas atlas;
        if (!atlas.build(sceneMesh.getBVH(), lmConfig))
        {
            baking_.store(false);
            auto& dispatcher = ::events::EventDispatcher::instance();
            services::events::lightbake::BakeFailedNotification notif;
            notif.errorMessage = "Failed to build lightmap atlas";
            dispatcher.publish(notif);
            return;
        }
        progress_.store(0.3f);

        // Step 3: Collect lights (30%)
        spdlog::info("[LightBake] Step 3/4: Collecting lights...");
        auto lights = collectLightsFromScene();
        if (lights.empty())
        {
            baking_.store(false);
            auto& dispatcher = ::events::EventDispatcher::instance();
            services::events::lightbake::BakeFailedNotification notif;
            notif.errorMessage = "No static lights found in scene";
            dispatcher.publish(notif);
            return;
        }

        result.bakedLightCount = static_cast<uint32_t>(
            lights.directionalLights.size() + lights.pointLights.size() + lights.spotLights.size());

        // Step 4: Run baker (30% - 95%)
        spdlog::info("[LightBake] Step 4/4: Baking irradiance...");
        auto& lightmapData = atlas.getLightmapData();

        bool bakeSuccess = baker_.bake(sceneMesh, atlas, lights, lightmapData,
            [this](float p) { progress_.store(0.3f + p * 0.65f); }
        );

        if (!bakeSuccess)
        {
            baking_.store(false);
            if (!baker_.wasCancelled())
            {
                auto& dispatcher = ::events::EventDispatcher::instance();
                services::events::lightbake::BakeFailedNotification notif;
                notif.errorMessage = "Bake failed";
                dispatcher.publish(notif);
            }
            return;
        }

        // Save lightmap
        progress_.store(0.95f);
        std::string outputPath = config.outputPath;
        if (outputPath.empty())
        {
            outputPath = "lightmap." + FileExtension::lightmap;
        }

        if (!lightbake::LightmapAtlas::save(lightmapData, outputPath))
        {
            baking_.store(false);
            auto& dispatcher = ::events::EventDispatcher::instance();
            services::events::lightbake::BakeFailedNotification notif;
            notif.errorMessage = "Failed to save lightmap to: " + outputPath;
            dispatcher.publish(notif);
            return;
        }

        // Assign LightmapComponent to each baked entity
        assignLightmapComponents(lightmapData, outputPath, config.texelsPerUnit);

        auto endTime = std::chrono::high_resolution_clock::now();
        float elapsedSeconds = std::chrono::duration<float>(endTime - startTime).count();

        result.success = true;
        result.lightmapPath = outputPath;
        result.atlasWidth = lightmapData.width;
        result.atlasHeight = lightmapData.height;
        result.bakeTimeSeconds = elapsedSeconds;

        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            lastResult_ = result;
        }

        progress_.store(1.0f);
        baking_.store(false);

        // Publish completion notification
        auto& dispatcher = ::events::EventDispatcher::instance();
        services::events::lightbake::BakeCompletedNotification notif;
        notif.result = result;
        dispatcher.publish(notif);

        spdlog::info("[LightBake] Bake complete: {}x{} atlas, {} lights, {:.2f}s",
                     result.atlasWidth, result.atlasHeight, result.bakedLightCount, result.bakeTimeSeconds);
    }
}
