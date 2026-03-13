#include "WaterRenderAdapter.hpp"
#include "../../services/impl/scene/WaterService.hpp"
#include "../../services/data/WaterData.hpp"
#include "../../services/providers/physics/IPhysicsProvider.hpp"

namespace core
{
    void WaterRenderAdapter::setWaterService(services::WaterService* service)
    {
        waterService = service;
        // Forward the ocean height sampler if we already have one
        if (waterService && oceanHeightSampler)
            waterService->setOceanHeightSampler(oceanHeightSampler);
    }

    void WaterRenderAdapter::setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler)
    {
        oceanHeightSampler = std::move(sampler);
        if (waterService && oceanHeightSampler)
            waterService->setOceanHeightSampler(oceanHeightSampler);
    }

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

    std::vector<water::WaterTile*> WaterRenderAdapter::queryVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        if (!waterService)
        {
            return {};
        }

        return waterService->queryVisibleWaterTiles(frustum, cameraPosition);
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

    void WaterRenderAdapter::setDistanceCullingEnabled(bool enabled)
    {
        if (waterService) waterService->setDistanceCullingEnabled(enabled);
    }

    void WaterRenderAdapter::setMaxDrawDistance(float distance)
    {
        if (waterService) waterService->setMaxDrawDistance(distance);
    }

    bool WaterRenderAdapter::isOceanFFTEnabled() const
    {
        return waterService && waterService->isOceanFFTEnabled();
    }

    services::OceanFFTConfigData WaterRenderAdapter::getOceanFFTConfig() const
    {
        if (!waterService) return {};
        return waterService->getOceanFFTConfig();
    }

    uint32_t WaterRenderAdapter::getOceanFFTConfigVersion() const
    {
        if (!waterService) return 0;
        return waterService->getOceanFFTConfigVersion();
    }

    float WaterRenderAdapter::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        if (oceanHeightSampler)
            return oceanHeightSampler(worldXZ);
        return 0.0f;
    }

    float WaterRenderAdapter::getPhysicsGravity() const
    {
        if (waterService && waterService->getPhysicsProvider())
        {
            glm::vec3 g = waterService->getPhysicsProvider()->getGravity();
            return glm::length(g);
        }
        return 9.81f;
    }
}
