#pragma once
#include "../../services/providers/ILightBakeProvider.hpp"
#include "../../utilities/lightbake/BakeSceneMesh.hpp"
#include "../../utilities/lightbake/LightmapAtlas.hpp"
#include "../../utilities/lightbake/LightBaker.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include <atomic>
#include <future>
#include <mutex>

namespace core
{
    class LightBakeAdapter : public services::ILightBakeProvider
    {
    private:
        std::atomic<bool> baking{false};
        std::atomic<float> progress{0.0f};
        mutable std::mutex resultMutex;
        services::LightBakeResult lastResult;
        std::future<void> bakeFuture;

        lightbake::LightBaker baker;

        std::string lastLightmapPath;
        float lastTexelsPerUnit = 16.0f;
        events::SubscriptionToken sceneClearedToken;
        events::SubscriptionToken sceneLoadedToken;

        std::vector<services::TerrainLightmapTileInfo> terrainLightmapInfos;

        std::vector<lightbake::TerrainTileBakeInfo> lastTerrainTileInfos;

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
        lightbake::BakeLightSet collectLightsFromScene() const;
        lightbake::TerrainBakeGeometry collectTerrainGeometry();
        std::vector<lightbake::WaterBakeTile> collectWaterTiles() const;
        void assignLightmapComponents(const resource::LightmapData& lightmapData,
                                       const std::string& outputPath, float texelsPerUnit);
        void runBake(const services::LightBakeConfig& config);
    };
}
