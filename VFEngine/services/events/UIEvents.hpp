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

    // ============================================
    // UI Scroll Commands
    // ============================================

    struct AddUIScrollComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIScrollComponent"; }
    };

    struct RemoveUIScrollComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIScrollComponent"; }
    };

    struct SetUIScrollDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIScrollData scrollData;

        std::string_view getName() const override { return "SetUIScrollData"; }
    };

    struct SetScrollOffsetCommand : ICommand<bool> {
        services::EntityHandle entity;
        glm::vec2 offset;

        std::string_view getName() const override { return "SetScrollOffset"; }
    };

    // ============================================
    // UI Scroll Queries
    // ============================================

    struct HasUIScrollComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIScrollComponent"; }
    };

    struct GetUIScrollDataQuery : IQuery<std::optional<services::UIScrollData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIScrollData"; }
    };

    struct GetScrollOffsetQuery : IQuery<std::optional<glm::vec2>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetScrollOffset"; }
    };

    // ============================================
    // UI Layout Group Commands
    // ============================================

    struct AddUILayoutGroupComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUILayoutGroupComponent"; }
    };

    struct RemoveUILayoutGroupComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUILayoutGroupComponent"; }
    };

    struct SetUILayoutGroupDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UILayoutGroupData layoutGroupData;

        std::string_view getName() const override { return "SetUILayoutGroupData"; }
    };

    // ============================================
    // UI Layout Group Queries
    // ============================================

    struct HasUILayoutGroupComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUILayoutGroupComponent"; }
    };

    struct GetUILayoutGroupDataQuery : IQuery<std::optional<services::UILayoutGroupData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILayoutGroupData"; }
    };

    // ============================================
    // UI Label Commands
    // ============================================

    struct AddUILabelComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUILabelComponent"; }
    };

    struct RemoveUILabelComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUILabelComponent"; }
    };

    struct SetUILabelDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UILabelData labelData;

        std::string_view getName() const override { return "SetUILabelData"; }
    };

    // ============================================
    // UI Label Queries
    // ============================================

    struct HasUILabelComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUILabelComponent"; }
    };

    struct GetUILabelDataQuery : IQuery<std::optional<services::UILabelData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILabelData"; }
    };

    struct GetUILabelPreferredSizeQuery : IQuery<std::optional<glm::vec2>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUILabelPreferredSize"; }
    };

    // ============================================
    // UI Button Commands
    // ============================================

    struct AddUIButtonComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIButtonComponent"; }
    };

    struct RemoveUIButtonComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIButtonComponent"; }
    };

    struct SetUIButtonDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIButtonData buttonData;

        std::string_view getName() const override { return "SetUIButtonData"; }
    };

    // ============================================
    // UI Button Queries
    // ============================================

    struct HasUIButtonComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIButtonComponent"; }
    };

    struct GetUIButtonDataQuery : IQuery<std::optional<services::UIButtonData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIButtonData"; }
    };

    // ============================================
    // UI Button Notifications
    // ============================================

    struct UIButtonClickedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonClicked"; }
    };

    struct UIButtonPressedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonPressed"; }
    };

    struct UIButtonReleasedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonReleased"; }
    };

    struct UIButtonHoverEnterNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonHoverEnter"; }
    };

    struct UIButtonHoverExitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIButtonHoverExit"; }
    };

}
