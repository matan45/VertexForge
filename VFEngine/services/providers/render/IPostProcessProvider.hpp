#pragma once
#include "postprocess/PostProcessTypes.hpp"
#include <cstdint>

namespace services
{
    struct UpscaleStatus
    {
        bool streamlineAvailable = false;
        bool dlssSupported = false;
        postprocess::UpscaleMode activeMode = postprocess::UpscaleMode::Off;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
        bool dlssGSupported = false;
        bool frameGenActive = false;
        bool reflexSupported = false;
        bool reflexActive = false;
        postprocess::ReflexMode reflexMode = postprocess::ReflexMode::Off;
        bool latencyValid = false;
        uint32_t gpuFrameTimeUs = 0;
        uint32_t totalLatencyUs = 0;
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
