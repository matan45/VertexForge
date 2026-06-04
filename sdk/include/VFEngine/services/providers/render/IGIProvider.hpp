#pragma once
#include "../../../graphics/render/gi/GITypes.hpp"

namespace services
{
    class IGIProvider
    {
    public:
        virtual ~IGIProvider() = default;

        virtual void applyGISettings(const render::gi::GISettings& settings) = 0;
        virtual render::gi::GISettings getGISettings() const = 0;
        virtual void setGIEnabled(bool enabled) = 0;
        virtual bool isGIEnabled() const = 0;
        virtual render::gi::GIDebugStats getGIDebugStats() const = 0;
        virtual void setShowProbes(bool show) = 0;
        virtual void setShowCascadeBounds(bool show) = 0;
        virtual void setShowProbeValidity(bool show) = 0;
    };
}
