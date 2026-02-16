#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/UIEvents.hpp"

namespace services {

    // ========== UI Dropdown Operations ==========

    bool UIComponentService::addUIDropdownComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIDropdownComponent>();

        // Auto-add UIRectComponent if missing (dropdown needs rect for layout/hit-testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        // Auto-add UIImageComponent if missing (dropdown header background)
        if (!sceneEntity.hasComponent<components::UIImageComponent>()) {
            sceneEntity.addComponent<components::UIImageComponent>();
        }

        // Auto-add UILabelComponent if missing (displays selected text / placeholder)
        if (!sceneEntity.hasComponent<components::UILabelComponent>()) {
            auto& labelComp = sceneEntity.addComponent<components::UILabelComponent>();
            labelComp.text = "Select...";
            labelComp.horizontalAlignment = components::HorizontalAlignment::Left;
            labelComp.verticalAlignment = components::VerticalAlignment::Middle;
        }

        return true;
    }

    bool UIComponentService::removeUIDropdownComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        // If this dropdown was the active one, clear the tracker
        auto enttEntity = internal::fromHandle(entity);
        if (components::UIDropdownComponent::activeDropdownEntity == enttEntity) {
            components::UIDropdownComponent::activeDropdownEntity = entt::null;
        }

        sceneEntity.removeComponent<components::UIDropdownComponent>();
        return true;
    }

    bool UIComponentService::hasUIDropdownComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIDropdownComponent>();
    }

    std::optional<UIDropdownData> UIComponentService::getUIDropdownData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();

        UIDropdownData data;
        data.options.reserve(comp.options.size());
        for (const auto& opt : comp.options) {
            data.options.push_back({opt.text, opt.iconPath});
        }
        data.selectedIndex = comp.selectedIndex;
        data.placeholderText = comp.placeholderText;
        data.maxVisibleItems = comp.maxVisibleItems;
        data.interactable = comp.interactable;
        data.normalColor = comp.normalColor;
        data.hoveredColor = comp.hoveredColor;
        data.openColor = comp.openColor;
        data.disabledColor = comp.disabledColor;
        data.listBackgroundColor = comp.listBackgroundColor;
        data.itemNormalColor = comp.itemNormalColor;
        data.itemHoveredColor = comp.itemHoveredColor;
        data.fontPath = comp.fontPath;
        data.fontSize = comp.fontSize;
        data.colorTransitionDuration = comp.colorTransitionDuration;
        data.currentState = static_cast<uint8_t>(comp.currentState);
        data.isOpen = comp.isOpen;
        data.hoveredOptionIndex = comp.hoveredOptionIndex;
        return data;
    }

    bool UIComponentService::setUIDropdownData(EntityHandle entity, const UIDropdownData& dropdownData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        comp.options.clear();
        comp.options.reserve(dropdownData.options.size());
        for (const auto& opt : dropdownData.options) {
            comp.options.push_back({opt.text, opt.iconPath});
        }
        comp.selectedIndex = dropdownData.selectedIndex;
        comp.placeholderText = dropdownData.placeholderText;
        comp.maxVisibleItems = std::max(1, dropdownData.maxVisibleItems);
        comp.interactable = dropdownData.interactable;
        comp.normalColor = dropdownData.normalColor;
        comp.hoveredColor = dropdownData.hoveredColor;
        comp.openColor = dropdownData.openColor;
        comp.disabledColor = dropdownData.disabledColor;
        comp.listBackgroundColor = dropdownData.listBackgroundColor;
        comp.itemNormalColor = dropdownData.itemNormalColor;
        comp.itemHoveredColor = dropdownData.itemHoveredColor;
        comp.fontPath = dropdownData.fontPath;
        comp.fontSize = dropdownData.fontSize;
        comp.colorTransitionDuration = dropdownData.colorTransitionDuration;
        comp.currentState = static_cast<components::UIDropdownState>(dropdownData.currentState);
        return true;
    }

    bool UIComponentService::setUIDropdownSelectedIndex(EntityHandle entity, int selectedIndex) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        int previousIndex = comp.selectedIndex;

        // Clamp to valid range
        if (selectedIndex >= static_cast<int>(comp.options.size())) {
            selectedIndex = static_cast<int>(comp.options.size()) - 1;
        }

        comp.selectedIndex = selectedIndex;

        if (previousIndex != selectedIndex) {
            auto& dispatcher = events::EventDispatcher::instance();

            std::string entityName;
            if (sceneEntity.hasComponent<components::NameComponent>()) {
                entityName = sceneEntity.getComponent<components::NameComponent>().name;
            }

            events::ui::UIDropdownSelectionChangedNotification notif;
            notif.entity = entity;
            notif.entityName = entityName;
            notif.previousIndex = previousIndex;
            notif.newIndex = selectedIndex;
            notif.selectedValue = (selectedIndex >= 0 && selectedIndex < static_cast<int>(comp.options.size()))
                                      ? comp.options[selectedIndex].text : "";
            dispatcher.publish(notif);
        }

        return true;
    }

    bool UIComponentService::openUIDropdown(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        if (!comp.interactable || comp.isOpen) {
            return false;
        }

        // Close any currently active dropdown
        auto activeEntity = components::UIDropdownComponent::activeDropdownEntity;
        if (activeEntity != entt::null && registry.valid(activeEntity)) {
            if (registry.all_of<components::UIDropdownComponent>(activeEntity)) {
                auto& activeComp = registry.get<components::UIDropdownComponent>(activeEntity);
                activeComp.isOpen = false;
                activeComp.currentState = activeComp.interactable
                    ? components::UIDropdownState::Normal
                    : components::UIDropdownState::Disabled;

                // Publish closed notification for the old dropdown
                auto& dispatcher = events::EventDispatcher::instance();
                events::ui::UIDropdownClosedNotification closedNotif;
                closedNotif.entity = internal::toHandle(activeEntity);
                if (registry.all_of<components::NameComponent>(activeEntity)) {
                    closedNotif.entityName = registry.get<components::NameComponent>(activeEntity).name;
                }
                dispatcher.publish(closedNotif);
            }
        }

        comp.isOpen = true;
        comp.currentState = components::UIDropdownState::Open;
        components::UIDropdownComponent::activeDropdownEntity = internal::fromHandle(entity);

        // Publish opened notification
        auto& dispatcher = events::EventDispatcher::instance();
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }
        events::ui::UIDropdownOpenedNotification notif;
        notif.entity = entity;
        notif.entityName = entityName;
        dispatcher.publish(notif);

        return true;
    }

    bool UIComponentService::closeUIDropdown(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropdownComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropdownComponent>();
        if (!comp.isOpen) {
            return false;
        }

        comp.isOpen = false;
        comp.hoveredOptionIndex = -1;
        comp.listScrollOffset = 0.0f;
        comp.currentState = comp.interactable ? components::UIDropdownState::Normal
                                              : components::UIDropdownState::Disabled;

        auto enttEntity = internal::fromHandle(entity);
        if (components::UIDropdownComponent::activeDropdownEntity == enttEntity) {
            components::UIDropdownComponent::activeDropdownEntity = entt::null;
        }

        // Publish closed notification
        auto& dispatcher = events::EventDispatcher::instance();
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }
        events::ui::UIDropdownClosedNotification notif;
        notif.entity = entity;
        notif.entityName = entityName;
        dispatcher.publish(notif);

        return true;
    }

    // ========== UI Tabs Operations ==========

    bool UIComponentService::addUITabsComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UITabsComponent>();

        // Auto-add UIRectComponent if missing
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUITabsComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UITabsComponent>();
        return true;
    }

    bool UIComponentService::hasUITabsComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UITabsComponent>();
    }

    std::optional<UITabsData> UIComponentService::getUITabsData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UITabsComponent>();

        UITabsData data;
        data.tabBarPosition = static_cast<uint8_t>(comp.tabBarPosition);
        data.activeTabIndex = comp.activeTabIndex;
        data.previousTabIndex = comp.previousTabIndex;
        return data;
    }

    bool UIComponentService::setUITabsData(EntityHandle entity, const UITabsData& tabsData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITabsComponent>();
        comp.tabBarPosition = static_cast<components::TabBarPosition>(tabsData.tabBarPosition);
        comp.activeTabIndex = tabsData.activeTabIndex;
        comp.previousTabIndex = tabsData.previousTabIndex;
        return true;
    }

    bool UIComponentService::selectTab(EntityHandle entity, int tabIndex) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UITabsComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UITabsComponent>();

        // Store previous
        int previousTabIndex = comp.activeTabIndex;
        comp.previousTabIndex = previousTabIndex;
        comp.activeTabIndex = tabIndex;

        // Find tab bar (first child with UILayoutGroupComponent) and panels
        if (!sceneEntity.hasComponent<components::ChildrenComponent>()) {
            return false;
        }

        const auto& children = sceneEntity.getComponent<components::ChildrenComponent>().children;
        std::vector<entt::entity> panels;

        for (auto childEntity : children) {
            if (!registry.valid(childEntity)) continue;

            // Skip the tab bar child (first child with UILayoutGroupComponent)
            if (registry.all_of<components::UILayoutGroupComponent>(childEntity)) {
                continue;
            }

            panels.push_back(childEntity);
        }

        // Toggle panel visibility: only the panel at tabIndex is active
        for (int i = 0; i < static_cast<int>(panels.size()); ++i) {
            if (registry.all_of<components::NameComponent>(panels[i])) {
                auto& nameComp = registry.get<components::NameComponent>(panels[i]);
                nameComp.isActive = (i == tabIndex);
            }
        }

        // Get entity name for notifications
        std::string entityName;
        if (sceneEntity.hasComponent<components::NameComponent>()) {
            entityName = sceneEntity.getComponent<components::NameComponent>().name;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Always publish tab selected
        events::ui::UITabSelectedNotification selectedNotif;
        selectedNotif.entity = entity;
        selectedNotif.entityName = entityName;
        selectedNotif.tabIndex = tabIndex;
        dispatcher.publish(selectedNotif);

        // Only publish tab changed if actually changed
        if (previousTabIndex != tabIndex) {
            events::ui::UITabChangedNotification changedNotif;
            changedNotif.entity = entity;
            changedNotif.entityName = entityName;
            changedNotif.newTabIndex = tabIndex;
            changedNotif.previousTabIndex = previousTabIndex;
            dispatcher.publish(changedNotif);
        }

        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerDropdownTabsHandlers(events::EventDispatcher& dispatcher) {
        // Dropdown commands
        dispatcher.registerCommandHandler<events::ui::AddUIDropdownComponentCommand>(
            [this](const events::ui::AddUIDropdownComponentCommand& cmd) {
                return addUIDropdownComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIDropdownComponentCommand>(
            [this](const events::ui::RemoveUIDropdownComponentCommand& cmd) {
                return removeUIDropdownComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDropdownDataCommand>(
            [this](const events::ui::SetUIDropdownDataCommand& cmd) {
                return setUIDropdownData(cmd.entity, cmd.dropdownData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDropdownSelectedIndexCommand>(
            [this](const events::ui::SetUIDropdownSelectedIndexCommand& cmd) {
                return setUIDropdownSelectedIndex(cmd.entity, cmd.selectedIndex);
            });

        dispatcher.registerCommandHandler<events::ui::OpenUIDropdownCommand>(
            [this](const events::ui::OpenUIDropdownCommand& cmd) {
                return openUIDropdown(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::CloseUIDropdownCommand>(
            [this](const events::ui::CloseUIDropdownCommand& cmd) {
                return closeUIDropdown(cmd.entity);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIDropdownComponentQuery>(
            [this](const events::ui::HasUIDropdownComponentQuery& query) {
                return hasUIDropdownComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIDropdownDataQuery>(
            [this](const events::ui::GetUIDropdownDataQuery& query) {
                return getUIDropdownData(query.entity);
            });

        // Tabs commands
        dispatcher.registerCommandHandler<events::ui::AddUITabsComponentCommand>(
            [this](const events::ui::AddUITabsComponentCommand& cmd) {
                return addUITabsComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUITabsComponentCommand>(
            [this](const events::ui::RemoveUITabsComponentCommand& cmd) {
                return removeUITabsComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITabsDataCommand>(
            [this](const events::ui::SetUITabsDataCommand& cmd) {
                return setUITabsData(cmd.entity, cmd.tabsData);
            });

        dispatcher.registerCommandHandler<events::ui::SetUITabsActiveTabCommand>(
            [this](const events::ui::SetUITabsActiveTabCommand& cmd) {
                return selectTab(cmd.entity, cmd.tabIndex);
            });

        dispatcher.registerQueryHandler<events::ui::HasUITabsComponentQuery>(
            [this](const events::ui::HasUITabsComponentQuery& query) {
                return hasUITabsComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUITabsDataQuery>(
            [this](const events::ui::GetUITabsDataQuery& query) {
                return getUITabsData(query.entity);
            });
    }

}
