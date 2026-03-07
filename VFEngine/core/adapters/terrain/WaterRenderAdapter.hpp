#pragma once

#include "../../services/providers/terrain/IWaterRenderProvider.hpp"
#include <functional>

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
        std::function<float(const glm::vec2&)> oceanHeightSampler;

    public:
        WaterRenderAdapter() = default;
        ~WaterRenderAdapter() override = default;

        void setWaterService(services::WaterService* service);
        void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) override;

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

        bool isOceanFFTEnabled() const override;
        services::OceanFFTConfigData getOceanFFTConfig() const override;
        uint32_t getOceanFFTConfigVersion() const override;
        float getOceanHeightAt(const glm::vec2& worldXZ) const override;
        float getPhysicsGravity() const override;
    };
}
