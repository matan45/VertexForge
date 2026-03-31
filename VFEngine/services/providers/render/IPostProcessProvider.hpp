#pragma once
#include "postprocess/PostProcessTypes.hpp"
#include "events/render/PostProcessEvents.hpp"

namespace services
{
    class IPostProcessProvider
    {
    public:
        virtual ~IPostProcessProvider() = default;

        virtual void applyPostProcessSettings(const postprocess::PostProcessSettings& settings) = 0;
        virtual postprocess::PostProcessSettings getPostProcessSettings() const = 0;
        virtual void setPostProcessEnabled(bool enabled) = 0;
        virtual bool isPostProcessEnabled() const = 0;
        virtual events::postprocess::UpscaleStatus getUpscaleStatus() const = 0;
    };
}
