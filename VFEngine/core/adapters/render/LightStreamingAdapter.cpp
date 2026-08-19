#include "LightStreamingAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core::adapters
{
    void LightStreamingAdapter::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
    {
        if (offScreen)
        {
            offScreen->setLightStreamingConfig(config);
        }
    }

    render::lighting::LightStreamingConfig LightStreamingAdapter::getLightStreamingConfig() const
    {
        if (offScreen)
        {
            return offScreen->getLightStreamingConfig();
        }
        return {};
    }

    render::lighting::LightStreamingStats LightStreamingAdapter::getLightStreamingStats() const
    {
        if (offScreen)
        {
            return offScreen->getLightStreamingStats();
        }
        return {};
    }

    void LightStreamingAdapter::registerSectorLights(uint64_t sectorId, const std::vector<uint32_t>& lightEntityIds)
    {
        if (offScreen)
        {
            offScreen->registerSectorLights(sectorId, lightEntityIds);
        }
    }

    void LightStreamingAdapter::unregisterSectorLights(uint64_t sectorId)
    {
        if (offScreen)
        {
            offScreen->unregisterSectorLights(sectorId);
        }
    }
}
