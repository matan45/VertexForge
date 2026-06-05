#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>

namespace events::scene
{
    struct AddFogVolumeComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "AddFogVolumeComponent"; }
    };

    struct RemoveFogVolumeComponentCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveFogVolumeComponent"; }
    };

    struct SetFogVolumeDataCommand : ::events::ICommand<bool>
    {
        services::EntityHandle entity;
        services::FogVolumeData data;
        std::string_view getName() const override { return "SetFogVolumeData"; }
    };

    struct HasFogVolumeComponentQuery : ::events::IQuery<bool>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasFogVolumeComponent"; }
    };

    struct GetFogVolumeDataQuery : ::events::IQuery<std::optional<services::FogVolumeData>>
    {
        services::EntityHandle entity;
        std::string_view getName() const override { return "GetFogVolumeData"; }
    };
}
