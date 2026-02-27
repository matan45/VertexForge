#pragma once
#include "../../services/providers/ILightBakeProvider.hpp"
#include "../../utilities/lightbake/BakeSceneMesh.hpp"
#include "../../utilities/lightbake/LightmapAtlas.hpp"
#include "../../utilities/lightbake/LightBaker.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include <memory>
#include <atomic>
#include <future>
#include <mutex>
#include <unordered_map>

namespace core
{
    class LightBakeAdapter : public services::ILightBakeProvider
    {
    public:
        LightBakeAdapter();
        ~LightBakeAdapter() noexcept override;

        void startBake(const services::LightBakeConfig& config) override;
        void cancelBake() override;
        float getBakeProgress() const override;
        bool isBaking() const override;
        services::LightBakeResult getResult() const override;
        bool loadLightmap(const std::string& path, float texelsPerUnit) override;
        void clearLightmap() override;
        std::vector<services::TerrainLightmapTileInfo> getTerrainLightmapData() const override;

    private:
        std::atomic<bool> baking_{false};
        std::atomic<float> progress_{0.0f};
        mutable std::mutex resultMutex_;
        services::LightBakeResult lastResult_;
        std::future<void> bakeFuture_;

        lightbake::LightBaker baker_;

        // Last applied lightmap info for re-applying after scene load
        std::string lastLightmapPath_;
        float lastTexelsPerUnit_ = 16.0f;
        events::SubscriptionToken sceneClearedToken_;
        events::SubscriptionToken sceneLoadedToken_;

        // Terrain lightmap data from last bake
        std::vector<services::TerrainLightmapTileInfo> terrainLightmapInfos_;

        // Tile bake info from last terrain geometry collection (used to map synthetic entityIds back to coords)
        std::vector<lightbake::TerrainTileBakeInfo> lastTerrainTileInfos_;

        // Collect lights from the ECS for baking
        lightbake::BakeLightSet collectLightsFromScene() const;

        // Collect terrain/water geometry for BVH
        lightbake::TerrainBakeGeometry collectTerrainGeometry();
        std::vector<lightbake::WaterBakeTile> collectWaterTiles() const;

        // Assign LightmapComponent to baked entities
        void assignLightmapComponents(const resource::LightmapData& lightmapData,
                                       const std::string& outputPath, float texelsPerUnit);

        // Run the full bake pipeline on a background thread
        void runBake(const services::LightBakeConfig& config);
    };
}
