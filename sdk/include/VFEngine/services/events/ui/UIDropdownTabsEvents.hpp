#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <optional>
#include <string>

namespace events::ui {

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

}
