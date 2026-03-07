#pragma once

#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include <cstdint>

namespace math
{
    class Frustum;
}

namespace water
{
    struct WaterTile;
    struct WaterTileConfig;
    struct WaterGlobalSettings;
}

namespace services
{
    struct OceanFFTConfigData;

    class IWaterRenderProvider
    {
    public:
        virtual ~IWaterRenderProvider() = default;

        virtual std::vector<water::WaterTile*> getVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        /// Lightweight frustum+distance query that does NOT modify tile state.
        /// Used for RTT frustum merging.
        virtual std::vector<water::WaterTile*> queryVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        virtual bool hasActiveWater() const = 0;

        virtual water::WaterGlobalSettings getWaterGlobalSettings() const = 0;

        virtual water::WaterTileConfig getWaterTileConfig() const = 0;

        virtual void setDistanceCullingEnabled(bool enabled) = 0;
        virtual void setMaxDrawDistance(float distance) = 0;

        // Ocean FFT
        virtual bool isOceanFFTEnabled() const = 0;
        virtual OceanFFTConfigData getOceanFFTConfig() const = 0;
        virtual uint32_t getOceanFFTConfigVersion() const = 0;
        virtual float getOceanHeightAt(const glm::vec2& worldXZ) const = 0;
        virtual void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) = 0;
        virtual float getPhysicsGravity() const = 0;
    };
}
