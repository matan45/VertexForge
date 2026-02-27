#pragma once
#include "../../services/providers/ILightBakeProvider.hpp"
#include "../../utilities/lightbake/BakeSceneMesh.hpp"
#include "../../utilities/lightbake/LightmapAtlas.hpp"
#include "../../utilities/lightbake/LightBaker.hpp"
#include <memory>
#include <atomic>
#include <future>
#include <mutex>

namespace core
{
    class LightBakeAdapter : public services::ILightBakeProvider
    {
    public:
        LightBakeAdapter() = default;
        ~LightBakeAdapter() noexcept override;

        void startBake(const services::LightBakeConfig& config) override;
        void cancelBake() override;
        float getBakeProgress() const override;
        bool isBaking() const override;
        services::LightBakeResult getResult() const override;

    private:
        std::atomic<bool> baking_{false};
        std::atomic<float> progress_{0.0f};
        mutable std::mutex resultMutex_;
        services::LightBakeResult lastResult_;
        std::future<void> bakeFuture_;

        lightbake::LightBaker baker_;

        // Collect lights from the ECS for baking
        lightbake::BakeLightSet collectLightsFromScene() const;

        // Collect terrain/water geometry for BVH
        lightbake::TerrainBakeGeometry collectTerrainGeometry() const;
        std::vector<lightbake::WaterBakeTile> collectWaterTiles() const;

        // Assign LightmapComponent to baked entities
        void assignLightmapComponents(const resource::LightmapData& lightmapData,
                                       const std::string& outputPath, float texelsPerUnit);

        // Run the full bake pipeline on a background thread
        void runBake(const services::LightBakeConfig& config);
    };
}
