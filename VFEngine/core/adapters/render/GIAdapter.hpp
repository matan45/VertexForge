#pragma once
#include "../../services/providers/render/IGIProvider.hpp"

namespace render::gpudriven
{
    class GPUDrivenRenderer;
}

namespace core
{
    class GIAdapter : public services::IGIProvider
    {
    private:
        render::gpudriven::GPUDrivenRenderer* renderer = nullptr;

    public:
        GIAdapter() = default;
        ~GIAdapter() override = default;

        void setRenderer(render::gpudriven::GPUDrivenRenderer* r) { renderer = r; }

        void applyGISettings(const render::gi::GISettings& settings) override
        {
            if (renderer)
            {
                renderer->applyGISettings(settings);
            }
        }

        render::gi::GISettings getGISettings() const override
        {
            if (renderer)
            {
                return renderer->getGISettings();
            }
            return {};
        }

        void setGIEnabled(bool enabled) override
        {
            if (renderer)
            {
                auto settings = renderer->getGISettings();
                settings.enabled = enabled;
                renderer->applyGISettings(settings);
            }
        }

        bool isGIEnabled() const override
        {
            if (renderer)
            {
                return renderer->getGISettings().enabled;
            }
            return false;
        }

        render::gi::GIDebugStats getGIDebugStats() const override
        {
            if (renderer && renderer->getGICascadeManager())
            {
                render::gi::GIDebugStats stats{};
                stats.totalProbes = renderer->getGICascadeManager()->getTotalProbeCount();
                stats.activeCascades = static_cast<uint32_t>(
                    renderer->getGICascadeManager()->getCascadeCount());
                return stats;
            }
            return {};
        }

        void setShowProbes(bool show) override
        {
            if (renderer && renderer->getGIDebugRenderer())
            {
                renderer->getGIDebugRenderer()->setShowProbes(show);
            }
        }

        void setShowCascadeBounds(bool show) override
        {
            if (renderer && renderer->getGIDebugRenderer())
            {
                renderer->getGIDebugRenderer()->setShowCascadeBounds(show);
            }
        }

        void setShowProbeValidity(bool show) override
        {
            if (renderer && renderer->getGIDebugRenderer())
            {
                renderer->getGIDebugRenderer()->setShowProbeValidity(show);
            }
        }

        void setIndirectOnlyMode(bool enabled) override
        {
            if (renderer && renderer->getGIDebugRenderer())
            {
                renderer->getGIDebugRenderer()->setIndirectOnlyMode(enabled);
            }
        }
    };
}
