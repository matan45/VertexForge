#pragma once
#include "../EventTypes.hpp"
#include "cloud/CloudSettings.hpp"

namespace events::cloud
{
    struct ApplyCloudSettingsCommand : ICommand<>
    {
        ::render::cloud::CloudSettings settings;

        std::string_view getName() const override { return "ApplyCloudSettings"; }
    };

    struct GetCloudSettingsQuery : IQuery<::render::cloud::CloudSettings>
    {
        std::string_view getName() const override { return "GetCloudSettings"; }
    };

    struct SetCloudEnabledCommand : ICommand<>
    {
        bool enabled;

        std::string_view getName() const override { return "SetCloudEnabled"; }
    };

    struct GetCloudEnabledQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "GetCloudEnabled"; }
    };
}
