#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <optional>

namespace events::cave
{
    struct SetCaveModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetCaveModeActive"; }
    };

    struct IsCaveModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsCaveModeActive"; }
    };

    struct GetCaveTargetEntityQuery : IQuery<std::optional<services::EntityHandle>>
    {
        std::string_view getName() const override { return "GetCaveTargetEntity"; }
    };

    struct CaveModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "CaveModeChanged"; }
    };
}
