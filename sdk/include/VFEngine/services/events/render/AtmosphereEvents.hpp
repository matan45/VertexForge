#pragma once
#include "../EventTypes.hpp"
#include "atmosphere/AtmosphereSettings.hpp"

namespace events::atmosphere
{
    struct ApplyAtmosphereSettingsCommand : ICommand<>
    {
        ::render::atmosphere::AtmosphereSettings settings;

        std::string_view getName() const override { return "ApplyAtmosphereSettings"; }
    };

    struct GetAtmosphereSettingsQuery : IQuery<::render::atmosphere::AtmosphereSettings>
    {
        std::string_view getName() const override { return "GetAtmosphereSettings"; }
    };

    struct SetAtmosphereEnabledCommand : ICommand<>
    {
        bool enabled;

        std::string_view getName() const override { return "SetAtmosphereEnabled"; }
    };

    struct GetAtmosphereEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetAtmosphereEnabled"; }
    };

    // VK-1566: per-frame day-night advance. Dispatched from the SunSync frame-graph step
    // (before the Transforms/worldMatrix bake). The handler advances the cycle on the live
    // atmosphere settings and, when cycleControlsSunEntity is set, rotates the sun entity.
    struct UpdateDayNightCommand : ICommand<>
    {
        float deltaTime = 0.0f;

        std::string_view getName() const override { return "UpdateDayNight"; }
    };
}
