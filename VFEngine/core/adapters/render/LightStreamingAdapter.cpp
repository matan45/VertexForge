#include "LightStreamingAdapter.hpp"
#include "../../../graphics/render/lighting/LightStreamManager.hpp"

namespace core::adapters
{
    void LightStreamingAdapter::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
    {
        if (streamManager)
        {
            streamManager->setConfig(config);
        }
    }

    render::lighting::LightStreamingConfig LightStreamingAdapter::getLightStreamingConfig() const
    {
        if (streamManager)
        {
            return streamManager->getConfig();
        }
        return {};
    }

    render::lighting::LightStreamingStats LightStreamingAdapter::getLightStreamingStats() const
    {
        if (streamManager)
        {
            return streamManager->getStats();
        }
        return {};
    }

    void LightStreamingAdapter::registerSectorLights(uint32_t sectorId)
    {
        if (streamManager)
        {
            streamManager->registerSectorLights(sectorId, {});
        }
    }

    void LightStreamingAdapter::unregisterSectorLights(uint32_t sectorId)
    {
        if (streamManager)
        {
            streamManager->unregisterSectorLights(sectorId);
        }
    }
}
