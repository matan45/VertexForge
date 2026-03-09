#pragma once
#include "../../../services/providers/render/IGIProvider.hpp"

namespace render::gi
{
    class RadianceCascadeManager;
    class GIDebugRenderer;
}

namespace core::adapters
{
    class GIAdapter : public services::IGIProvider
    {
    private:
        render::gi::RadianceCascadeManager* cascadeManager = nullptr;
        render::gi::GIDebugRenderer* debugRenderer = nullptr;
        render::gi::GISettings cachedSettings;

    public:
        GIAdapter() = default;
        ~GIAdapter() override = default;

        void setCascadeManager(render::gi::RadianceCascadeManager* mgr) { cascadeManager = mgr; }
        void setDebugRenderer(render::gi::GIDebugRenderer* dbg) { debugRenderer = dbg; }

        void applyGISettings(const render::gi::GISettings& settings) override;
        render::gi::GISettings getGISettings() const override;
        void setGIEnabled(bool enabled) override;
        bool isGIEnabled() const override;
        render::gi::GIDebugStats getGIDebugStats() const override;
        void setShowProbes(bool show) override;
        void setShowCascadeBounds(bool show) override;
        void setShowProbeValidity(bool show) override;
        void setIndirectOnlyMode(bool enabled) override;
    };
}
