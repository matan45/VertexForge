#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <optional>

namespace events::hole
{
    struct SetHoleModeActiveCommand : ICommand<>
    {
        bool active;

        std::string_view getName() const override { return "SetHoleModeActive"; }
    };

    struct IsHoleModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsHoleModeActive"; }
    };

    struct GetHoleTargetEntityQuery : IQuery<std::optional<services::EntityHandle>>
    {
        std::string_view getName() const override { return "GetHoleTargetEntity"; }
    };

    struct HoleModeChangedNotification : INotification
    {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "HoleModeChanged"; }
    };
}
