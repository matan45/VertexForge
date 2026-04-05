#pragma once
#include "postprocess/PostProcessTypes.hpp"
#include <cstdint>

namespace services
{
    struct UpscaleStatus
    {
        bool streamlineAvailable = false;
        bool dlssSupported = false;
        bool directSRSupported = false;
        postprocess::UpscaleMode activeMode = postprocess::UpscaleMode::Off;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
    };

    class IPostProcessProvider
    {
    public:
        virtual ~IPostProcessProvider() = default;

        virtual void applyPostProcessSettings(const postprocess::PostProcessSettings& settings) = 0;
        virtual postprocess::PostProcessSettings getPostProcessSettings() const = 0;
        virtual void setPostProcessEnabled(bool enabled) = 0;
        virtual bool isPostProcessEnabled() const = 0;
        virtual UpscaleStatus getUpscaleStatus() const = 0;
    };
}
