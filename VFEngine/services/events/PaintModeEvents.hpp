#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include <optional>

namespace events::paint {

    struct SetPaintModeActiveCommand : ICommand<> {
        bool active;

        std::string_view getName() const override { return "SetPaintModeActive"; }
    };

    struct IsPaintModeActiveQuery : IQuery<bool> {
        std::string_view getName() const override { return "IsPaintModeActive"; }
    };

    struct GetPaintTargetEntityQuery : IQuery<std::optional<services::EntityHandle>> {
        std::string_view getName() const override { return "GetPaintTargetEntity"; }
    };

    struct PaintModeChangedNotification : INotification {
        bool isActive;
        std::optional<services::EntityHandle> terrainEntity;

        std::string_view getName() const override { return "PaintModeChanged"; }
    };

}
