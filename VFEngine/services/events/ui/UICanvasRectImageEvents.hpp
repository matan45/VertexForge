#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
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

    // Position/size a UIRect in viewport pixels (top-left origin, y down) regardless
    // of its authored anchors/pivot. Solves anchoredPosition/sizeDelta so the runtime
    // screen-space pass (resolvePixelRect) lands exactly on the requested rect —
    // keeps scripts in the same pixel space as Input::getViewportMouseX/Y.
    struct SetUIRectPixelsCommand : ICommand<bool> {
        services::EntityHandle entity;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        std::string_view getName() const override { return "SetUIRectPixels"; }
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

    // ============================================
    // UI Image Commands
    // ============================================

    struct AddUIImageComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIImageComponent"; }
    };

    struct RemoveUIImageComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIImageComponent"; }
    };

    struct SetUIImageDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIImageData imageData;

        std::string_view getName() const override { return "SetUIImageData"; }
    };

    // ============================================
    // UI Image Queries
    // ============================================

    struct HasUIImageComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIImageComponent"; }
    };

    struct GetUIImageDataQuery : IQuery<std::optional<services::UIImageData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIImageData"; }
    };

}
