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

    // ============================================
    // UI TextInput Commands
    // ============================================

    struct AddUITextInputComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUITextInputComponent"; }
    };

    struct RemoveUITextInputComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUITextInputComponent"; }
    };

    struct SetUITextInputDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UITextInputData textInputData;

        std::string_view getName() const override { return "SetUITextInputData"; }
    };

    // ============================================
    // UI TextInput Queries
    // ============================================

    struct HasUITextInputComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUITextInputComponent"; }
    };

    struct GetUITextInputDataQuery : IQuery<std::optional<services::UITextInputData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUITextInputData"; }
    };

    // ============================================
    // UI TextInput Notifications
    // ============================================

    struct UITextInputSubmitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        std::string text;

        std::string_view getName() const override { return "UITextInputSubmit"; }
    };

    struct UITextInputChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        std::string text;

        std::string_view getName() const override { return "UITextInputChanged"; }
    };

    struct UITextInputFocusedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UITextInputFocused"; }
    };

    struct UITextInputUnfocusedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UITextInputUnfocused"; }
    };

    // ============================================
    // UI Checkbox Commands
    // ============================================

    struct AddUICheckboxComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUICheckboxComponent"; }
    };

    struct RemoveUICheckboxComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUICheckboxComponent"; }
    };

    struct SetUICheckboxDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UICheckboxData checkboxData;

        std::string_view getName() const override { return "SetUICheckboxData"; }
    };

    // ============================================
    // UI Checkbox Queries
    // ============================================

    struct HasUICheckboxComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUICheckboxComponent"; }
    };

    struct GetUICheckboxDataQuery : IQuery<std::optional<services::UICheckboxData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUICheckboxData"; }
    };

    // ============================================
    // UI Checkbox Notifications
    // ============================================

    struct UICheckboxToggledNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        bool newCheckedState;
        bool previousCheckedState;

        std::string_view getName() const override { return "UICheckboxToggled"; }
    };

    struct UICheckboxHoverEnterNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UICheckboxHoverEnter"; }
    };

    struct UICheckboxHoverExitNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UICheckboxHoverExit"; }
    };

    // ============================================
    // UI Dropdown Commands
    // ============================================

    struct AddUIDropdownComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUIDropdownComponent"; }
    };

    struct RemoveUIDropdownComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUIDropdownComponent"; }
    };

    struct SetUIDropdownDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UIDropdownData dropdownData;

        std::string_view getName() const override { return "SetUIDropdownData"; }
    };

    struct SetUIDropdownSelectedIndexCommand : ICommand<bool> {
        services::EntityHandle entity;
        int selectedIndex;

        std::string_view getName() const override { return "SetUIDropdownSelectedIndex"; }
    };

    struct OpenUIDropdownCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "OpenUIDropdown"; }
    };

    struct CloseUIDropdownCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "CloseUIDropdown"; }
    };

    // ============================================
    // UI Dropdown Queries
    // ============================================

    struct HasUIDropdownComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUIDropdownComponent"; }
    };

    struct GetUIDropdownDataQuery : IQuery<std::optional<services::UIDropdownData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIDropdownData"; }
    };

    // ============================================
    // UI Dropdown Notifications
    // ============================================

    struct UIDropdownOpenedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIDropdownOpened"; }
    };

    struct UIDropdownClosedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;

        std::string_view getName() const override { return "UIDropdownClosed"; }
    };

    struct UIDropdownSelectionChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        int previousIndex;
        int newIndex;
        std::string selectedValue;

        std::string_view getName() const override { return "UIDropdownSelectionChanged"; }
    };

    // ============================================
    // UI Tabs Commands
    // ============================================

    struct AddUITabsComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "AddUITabsComponent"; }
    };

    struct RemoveUITabsComponentCommand : ICommand<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "RemoveUITabsComponent"; }
    };

    struct SetUITabsDataCommand : ICommand<bool> {
        services::EntityHandle entity;
        services::UITabsData tabsData;

        std::string_view getName() const override { return "SetUITabsData"; }
    };

    struct SetUITabsActiveTabCommand : ICommand<bool> {
        services::EntityHandle entity;
        int tabIndex;

        std::string_view getName() const override { return "SetUITabsActiveTab"; }
    };

    // ============================================
    // UI Tabs Queries
    // ============================================

    struct HasUITabsComponentQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasUITabsComponent"; }
    };

    struct GetUITabsDataQuery : IQuery<std::optional<services::UITabsData>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUITabsData"; }
    };

    // ============================================
    // UI Tabs Notifications
    // ============================================

    struct UITabSelectedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        int tabIndex;

        std::string_view getName() const override { return "UITabSelected"; }
    };

    struct UITabChangedNotification : INotification {
        services::EntityHandle entity;
        std::string entityName;
        int newTabIndex;
        int previousTabIndex;

        std::string_view getName() const override { return "UITabChanged"; }
    };

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

}
