#pragma once
#include "../EventTypes.hpp"
#include "../../../graphics/render/gi/GITypes.hpp"

namespace events::render::gi
{
    // ---- Commands ----

    struct ApplyGISettingsCommand : ::events::ICommand<void>
    {
        ::render::gi::GISettings settings;
        std::string_view getName() const override { return "ApplyGISettings"; }
    };

    struct SetGIQualityCommand : ::events::ICommand<void>
    {
        ::render::gi::GIQuality quality;
        std::string_view getName() const override { return "SetGIQuality"; }
    };

    struct SetGIEnabledCommand : ::events::ICommand<void>
    {
        bool enabled;
        std::string_view getName() const override { return "SetGIEnabled"; }
    };

    struct SetGIDebugProbesCommand : ::events::ICommand<void>
    {
        bool show;
        std::string_view getName() const override { return "SetGIDebugProbes"; }
    };

    struct SetGIDebugCascadeBoundsCommand : ::events::ICommand<void>
    {
        bool show;
        std::string_view getName() const override { return "SetGIDebugCascadeBounds"; }
    };

    struct SetGIDebugProbeValidityCommand : ::events::ICommand<void>
    {
        bool show;
        std::string_view getName() const override { return "SetGIDebugProbeValidity"; }
    };

    // ---- Queries ----

    struct GetGISettingsQuery : ::events::IQuery<::render::gi::GISettings>
    {
        std::string_view getName() const override { return "GetGISettings"; }
    };

    struct GetGIDebugStatsQuery : ::events::IQuery<::render::gi::GIDebugStats>
    {
        std::string_view getName() const override { return "GetGIDebugStats"; }
    };

    struct IsGIEnabledQuery : ::events::IQuery<bool>
    {
        std::string_view getName() const override { return "IsGIEnabled"; }
    };

    // ---- Notifications ----

    struct GISettingsChangedNotification : ::events::INotification
    {
        ::render::gi::GISettings settings;
        std::string_view getName() const override { return "GISettingsChanged"; }
    };
}
