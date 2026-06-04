#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Mask Commands
    // ============================================

    struct AddUIMaskComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIMaskComponent"; }
    };

    struct RemoveUIMaskComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIMaskComponent"; }
    };

    struct SetUIMaskDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIMaskData maskData;

        std::string_view getName() const override { return "SetUIMaskData"; }
    };

    // ============================================
    // UI Mask Queries
    // ============================================

    struct HasUIMaskComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIMaskComponent"; }
    };

    struct GetUIMaskDataQuery : IQuery<std::optional<services::UIMaskData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIMaskData"; }
    };

}
