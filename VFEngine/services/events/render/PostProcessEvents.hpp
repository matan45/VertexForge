#pragma once
#include "../EventTypes.hpp"
#include "postprocess/PostProcessTypes.hpp"
#include "../../providers/render/IPostProcessProvider.hpp"

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

    struct GetUpscaleStatusQuery : IQuery<services::UpscaleStatus>
    {
        std::string_view getName() const override { return "GetUpscaleStatus"; }
    };
}
