#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>

namespace events::ui {

    // ============================================
    // UI Tooltip Commands
    // ============================================

    struct AddUITooltipComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUITooltipComponent"; }
    };

    struct RemoveUITooltipComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUITooltipComponent"; }
    };

    struct SetUITooltipDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UITooltipData tooltipData;

        std::string_view getName() const override { return "SetUITooltipData"; }
    };

    // ============================================
    // UI Tooltip Queries
    // ============================================

    struct HasUITooltipComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUITooltipComponent"; }
    };

    struct GetUITooltipDataQuery : IQuery<std::optional<services::UITooltipData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUITooltipData"; }
    };

}
