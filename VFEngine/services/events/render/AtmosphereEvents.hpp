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
}
