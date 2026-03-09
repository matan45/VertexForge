#include "GIAdapter.hpp"
#include "../../../graphics/render/gi/RadianceCascadeManager.hpp"
#include "../../../graphics/render/gi/GIDebugRenderer.hpp"

namespace core::adapters
{
    void GIAdapter::applyGISettings(const render::gi::GISettings& settings)
    {
        cachedSettings = settings;
        if (cascadeManager)
        {
            cascadeManager->applySettings(settings);
        }
    }

    render::gi::GISettings GIAdapter::getGISettings() const
    {
        return cachedSettings;
    }

    void GIAdapter::setGIEnabled(bool enabled)
    {
        cachedSettings.enabled = enabled;
        if (cascadeManager)
        {
            cascadeManager->applySettings(cachedSettings);
        }
    }

    bool GIAdapter::isGIEnabled() const
    {
        return cachedSettings.enabled;
    }

    render::gi::GIDebugStats GIAdapter::getGIDebugStats() const
    {
        if (cascadeManager)
        {
            render::gi::GIDebugStats stats{};
            stats.totalProbes = cascadeManager->getTotalProbeCount();
            stats.activeCascades = static_cast<uint32_t>(
                cascadeManager->getCascadeCount());
            return stats;
        }
        return {};
    }

    void GIAdapter::setShowProbes(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowProbes(show);
        }
    }

    void GIAdapter::setShowCascadeBounds(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowCascadeBounds(show);
        }
    }

    void GIAdapter::setShowProbeValidity(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowProbeValidity(show);
        }
    }

    void GIAdapter::setIndirectOnlyMode(bool enabled)
    {
        if (debugRenderer)
        {
            debugRenderer->setIndirectOnlyMode(enabled);
        }
    }
}
