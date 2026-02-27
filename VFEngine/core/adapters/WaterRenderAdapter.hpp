#pragma once

#include "../../services/providers/IWaterRenderProvider.hpp"

namespace services
{
    class WaterService;
}

namespace core
{
    class WaterRenderAdapter : public services::IWaterRenderProvider
    {
    private:
        services::WaterService* waterService = nullptr;

    public:
        WaterRenderAdapter() = default;
        ~WaterRenderAdapter() override = default;

        void setWaterService(services::WaterService* service) { waterService = service; }

        std::vector<water::WaterTile*> getVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        std::vector<water::WaterTile*> queryVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        bool hasActiveWater() const override;

        water::WaterGlobalSettings getWaterGlobalSettings() const override;

        water::WaterTileConfig getWaterTileConfig() const override;

        void setDistanceCullingEnabled(bool enabled) override;
        void setMaxDrawDistance(float distance) override;
    };
}
