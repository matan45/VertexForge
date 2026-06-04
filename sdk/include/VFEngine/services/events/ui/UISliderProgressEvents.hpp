#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

    // ============================================
    // UI Slider Commands
    // ============================================

    struct AddUISliderComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUISliderComponent"; }
    };

    struct RemoveUISliderComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUISliderComponent"; }
    };

    struct SetUISliderDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UISliderData sliderData;

        std::string_view getName() const override { return "SetUISliderData"; }
    };

    struct SetUISliderValueCommand : ICommand<bool> {
        services::EntityHandle entity;
        float value;

        std::string_view getName() const override { return "SetUISliderValue"; }
    };

    // ============================================
    // UI Slider Queries
    // ============================================

    struct HasUISliderComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUISliderComponent"; }
    };

    struct GetUISliderDataQuery : IQuery<std::optional<services::UISliderData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUISliderData"; }
    };

    // ============================================
    // UI Slider Notifications
    // ============================================

    struct UISliderValueChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        float newValue;
        float previousValue;

        std::string_view getName() const override { return "UISliderValueChanged"; }
    };

    struct UISliderDragStartNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UISliderDragStart"; }
    };

    struct UISliderDragEndNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        float finalValue;

        std::string_view getName() const override { return "UISliderDragEnd"; }
    };

    struct UISliderHoverEnterNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UISliderHoverEnter"; }
    };

    struct UISliderHoverExitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UISliderHoverExit"; }
    };

    // ============================================
    // UI ProgressBar Commands
    // ============================================

    struct AddUIProgressBarComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIProgressBarComponent"; }
    };

    struct RemoveUIProgressBarComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIProgressBarComponent"; }
    };

    struct SetUIProgressBarDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIProgressBarData progressBarData;

        std::string_view getName() const override { return "SetUIProgressBarData"; }
    };

    struct SetUIProgressBarValueCommand : ICommand<bool> {
        services::EntityHandle entity;
        float value;

        std::string_view getName() const override { return "SetUIProgressBarValue"; }
    };

    // ============================================
    // UI ProgressBar Queries
    // ============================================

    struct HasUIProgressBarComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIProgressBarComponent"; }
    };

    struct GetUIProgressBarDataQuery : IQuery<std::optional<services::UIProgressBarData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIProgressBarData"; }
    };

    // ============================================
    // UI ProgressBar Notifications
    // ============================================

    struct UIProgressBarValueChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        float newValue;
        float previousValue;

        std::string_view getName() const override { return "UIProgressBarValueChanged"; }
    };

    struct UIProgressBarCompletedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIProgressBarCompleted"; }
    };

}
