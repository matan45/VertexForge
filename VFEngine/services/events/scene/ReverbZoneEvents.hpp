#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <string>
#include <vector>
#include <optional>

namespace events::scene
{
    struct AddReverbZoneComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AddReverbZoneComponent"; }
    };

    struct RemoveReverbZoneComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveReverbZoneComponent"; }
    };

    struct SetReverbZoneDataCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        services::ReverbZoneData data;
        std::string_view getName() const override { return "SetReverbZoneData"; }
    };

    struct HasReverbZoneComponentQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasReverbZoneComponent"; }
    };

    struct GetReverbZoneDataQuery : ::events::IQuery<std::optional<services::ReverbZoneData>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetReverbZoneData"; }
    };

    struct GetReverbPresetNamesQuery : ::events::IQuery<std::vector<std::string>>
    {
        std::string_view getName() const override { return "GetReverbPresetNames"; }
    };
}
