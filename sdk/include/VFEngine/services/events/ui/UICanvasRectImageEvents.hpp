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

    // VK-1435 — tag/untag an entity as a UI Layer Builder preview sandbox by
    // adding/removing UIPreviewTagComponent. The serializer skips a tagged child (and does
    // not recurse into it) and the main-pass UI enumeration skips a tagged canvas root, so
    // the builder's authoring subtree (which lives in the one singleton registry) never
    // renders in the main viewport nor bakes into the user's scene. The editor stays
    // entt-free and cannot add this engine-internal marker itself, so it issues this command
    // on the sandbox CANVAS ROOT right after creating/loading it (before the first preview
    // build/render). Teardown is a recursive DeleteEntityCommand on the root, so untag
    // (tagged=false) exists only for completeness.
    struct MarkUIPreviewSandboxCommand : ICommand<bool> {
        services::EntityHandle entity;
        bool tagged = true;

        std::string_view getName() const override { return "MarkUIPreviewSandbox"; }
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

    // Resolved on-screen pixel rect (viewport space, top-left origin, y down) of a
    // UIRect, accounting for canvas anchoring + ScaleWithScreenSize — the read-side
    // counterpart of SetUIRectPixelsCommand, resolved with the same math as the
    // runtime UI hit tests. nullopt if the entity has no UIRect or no viewport.
    struct GetUIResolvedRectQuery : IQuery<std::optional<services::UIResolvedRectData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIResolvedRect"; }
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
