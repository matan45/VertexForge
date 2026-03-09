#include "GIAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core::adapters
{
    void GIAdapter::applyGISettings(const render::gi::GISettings& settings)
    {
        if (offScreen)
        {
            offScreen->applyGISettings(settings);
        }
    }

    render::gi::GISettings GIAdapter::getGISettings() const
    {
        if (offScreen)
        {
            return offScreen->getGISettings();
        }
        return {};
    }

    void GIAdapter::setGIEnabled(bool enabled)
    {
        if (offScreen)
        {
            auto settings = offScreen->getGISettings();
            settings.enabled = enabled;
            offScreen->applyGISettings(settings);
        }
    }

    bool GIAdapter::isGIEnabled() const
    {
        if (offScreen)
        {
            return offScreen->getGISettings().enabled;
        }
        return false;
    }

    render::gi::GIDebugStats GIAdapter::getGIDebugStats() const
    {
        if (offScreen)
        {
            return offScreen->getGIDebugStats();
        }
        return {};
    }

    void GIAdapter::setShowProbes(bool show)
    {
        if (offScreen)
        {
            offScreen->setGIShowProbes(show);
        }
    }

    void GIAdapter::setShowCascadeBounds(bool show)
    {
        if (offScreen)
        {
            offScreen->setGIShowCascadeBounds(show);
        }
    }

    void GIAdapter::setShowProbeValidity(bool show)
    {
        if (offScreen)
        {
            offScreen->setGIShowProbeValidity(show);
        }
    }

    void GIAdapter::setIndirectOnlyMode(bool enabled)
    {
        if (offScreen)
        {
            offScreen->setGIIndirectOnlyMode(enabled);
        }
    }
}
