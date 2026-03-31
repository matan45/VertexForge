#pragma once
#include "../EventTypes.hpp"
#include "postprocess/PostProcessTypes.hpp"

namespace events::postprocess
{
    struct ApplyPostProcessSettingsCommand : ICommand<>
    {
        ::postprocess::PostProcessSettings settings;

        std::string_view getName() const override { return "ApplyPostProcessSettings"; }
    };

    struct GetPostProcessSettingsQuery : IQuery<::postprocess::PostProcessSettings>
    {
        std::string_view getName() const override { return "GetPostProcessSettings"; }
    };

    struct SetPostProcessEnabledCommand : ICommand<>
    {
        bool enabled;

        std::string_view getName() const override { return "SetPostProcessEnabled"; }
    };

    struct GetPostProcessEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetPostProcessEnabled"; }
    };

    struct UpscaleStatus
    {
        bool streamlineAvailable = false;
        bool dlssSupported = false;
        bool directSRSupported = false;
        ::postprocess::UpscaleMode activeMode = ::postprocess::UpscaleMode::Off;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
    };

    struct GetUpscaleStatusQuery : IQuery<UpscaleStatus>
    {
        std::string_view getName() const override { return "GetUpscaleStatus"; }
    };
}
