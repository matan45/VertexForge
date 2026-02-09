#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <optional>

namespace events::sculpt {

    struct SetSculptModeActiveCommand : ICommand<> {
        bool active;

        std::string_view getName() const override { return "SetSculptModeActive"; }
    };

    struct IsSculptModeActiveQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsSculptModeActive"; }
    };

    struct GetSculptTargetEntityQuery : IQuery<std::optional<services::EntityHandle>> {
        std::string_view getName() const override { return "GetSculptTargetEntity"; }
    };

    struct SculptModeChangedNotification : INotification {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "SculptModeChanged"; }
    };

}
