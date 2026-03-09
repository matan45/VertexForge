#pragma once
#include "../../../services/providers/render/IGIProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core::adapters
{
    class GIAdapter : public services::IGIProvider
    {
    private:
        controllers::OffScreen* offScreen = nullptr;

    public:
        GIAdapter() = default;
        ~GIAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller) { offScreen = controller; }

        void applyGISettings(const render::gi::GISettings& settings) override;
        render::gi::GISettings getGISettings() const override;
        void setGIEnabled(bool enabled) override;
        bool isGIEnabled() const override;
        render::gi::GIDebugStats getGIDebugStats() const override;
        void setShowProbes(bool show) override;
        void setShowCascadeBounds(bool show) override;
        void setShowProbeValidity(bool show) override;
    };
}
