#pragma once
#include "EventTypes.hpp"
#include "../data/EntityHandle.hpp"
#include "../data/DTOs.hpp"
#include <optional>

namespace events::ui {

    // ============================================
    // UI Canvas Commands
    // ============================================

    struct AddUICanvasComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUICanvasComponent"; }
    };

    struct RemoveUICanvasComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUICanvasComponent"; }
    };

    struct SetUICanvasDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UICanvasData canvasData;

        std::string_view getName() const override { return "SetUICanvasData"; }
    };

    // ============================================
    // UI Rect Commands
    // ============================================

    struct AddUIRectComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIRectComponent"; }
    };

    struct RemoveUIRectComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIRectComponent"; }
    };

    struct SetUIRectDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIRectData rectData;

        std::string_view getName() const override { return "SetUIRectData"; }
    };

    // ============================================
    // UI Canvas Queries
    // ============================================

    struct HasUICanvasComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUICanvasComponent"; }
    };

    struct GetUICanvasDataQuery : IQuery<std::optional<services::UICanvasData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUICanvasData"; }
    };

    // ============================================
    // UI Rect Queries
    // ============================================

    struct HasUIRectComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIRectComponent"; }
    };

    struct GetUIRectDataQuery : IQuery<std::optional<services::UIRectData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIRectData"; }
    };

}
