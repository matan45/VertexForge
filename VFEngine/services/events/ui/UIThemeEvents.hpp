#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Style / Theme Commands
    // ============================================

    struct AddUIStyleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIStyleComponent"; }
    };

    struct RemoveUIStyleComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIStyleComponent"; }
    };

    struct SetUIStyleKeyCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string styleKey;

        std::string_view getName() const override { return "SetUIStyleKey"; }
    };

    // Assigns a .vfTheme asset (project-relative or absolute path; empty
    // clears the theme) to a canvas and applies it to the subtree.
    struct SetCanvasThemeCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string themePath;

        std::string_view getName() const override { return "SetCanvasTheme"; }
    };

    // Re-applies canvas themes. No canvas = every canvas with a valid theme.
    // Returns the number of styled entities touched.
    struct ReapplyUIThemeCommand : ICommand<int> {
        std::optional<services::EntityHandle> canvas;

        std::string_view getName() const override { return "ReapplyUITheme"; }
    };

    // ============================================
    // UI Style / Theme Queries
    // ============================================

    struct HasUIStyleComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIStyleComponent"; }
    };

    struct GetUIStyleKeyQuery : IQuery<std::optional<std::string>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIStyleKey"; }
    };

    // Returns the canvas theme as a resolved asset path (empty = no theme).
    struct GetCanvasThemeQuery : IQuery<std::optional<std::string>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetCanvasTheme"; }
    };

}
