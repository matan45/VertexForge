#pragma once
#include "EventTypes.hpp"
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
}
