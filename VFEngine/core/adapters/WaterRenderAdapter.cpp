#include "WaterRenderAdapter.hpp"
#include "../../services/impl/scene/WaterService.hpp"

namespace core
{
    std::vector<water::WaterTile*> WaterRenderAdapter::getVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        if (!waterService)
        {
            return {};
        }

        return waterService->getVisibleWaterTiles(frustum, cameraPosition);
    }

    bool WaterRenderAdapter::hasActiveWater() const
    {
        return waterService && waterService->hasActiveWater();
    }

    water::WaterGlobalSettings WaterRenderAdapter::getWaterGlobalSettings() const
    {
        if (!waterService)
        {
            return {};
        }

        return waterService->getWaterGlobalSettings();
    }

    water::WaterTileConfig WaterRenderAdapter::getWaterTileConfig() const
    {
        if (!waterService)
        {
            return {};
        }

        return waterService->getWaterTileConfig();
    }
}
