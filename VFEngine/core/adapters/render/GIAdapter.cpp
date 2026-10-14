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

    // The debug flags go through the settings (no structural change, so no GI rebuild) so that
    // GetGISettingsQuery reports them and a later apply does not revert them.
    void GIAdapter::setShowProbes(bool show)
    {
        if (offScreen)
        {
            auto settings = offScreen->getGISettings();
            settings.showProbes = show;
            offScreen->applyGISettings(settings);
        }
    }

    void GIAdapter::setShowCascadeBounds(bool show)
    {
        if (offScreen)
        {
            auto settings = offScreen->getGISettings();
            settings.showCascadeBounds = show;
            offScreen->applyGISettings(settings);
        }
    }

    void GIAdapter::setShowProbeValidity(bool show)
    {
        if (offScreen)
        {
            auto settings = offScreen->getGISettings();
            settings.showProbeValidity = show;
            offScreen->applyGISettings(settings);
        }
    }

}
