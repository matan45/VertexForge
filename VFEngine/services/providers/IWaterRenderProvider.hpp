#pragma once

#include <vector>
#include <glm/glm.hpp>

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
    class IWaterRenderProvider
    {
    public:
        virtual ~IWaterRenderProvider() = default;

        virtual std::vector<water::WaterTile*> getVisibleWaterTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        virtual bool hasActiveWater() const = 0;

        virtual water::WaterGlobalSettings getWaterGlobalSettings() const = 0;

        virtual water::WaterTileConfig getWaterTileConfig() const = 0;
    };
}
