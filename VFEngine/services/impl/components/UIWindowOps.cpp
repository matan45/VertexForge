#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIWindowEvents.hpp"
#include <algorithm>

namespace services {

    namespace {
        void publishWindowNotification(entt::registry& registry, entt::entity entity, bool opened)
        {
            std::string name;
            if (auto* nameComp = registry.try_get<components::NameComponent>(entity))
                name = nameComp->name;

            if (opened) {
                events::ui::UIWindowOpenedNotification notif;
                notif.entity = internal::toHandle(entity);
                notif.entityName = std::move(name);
                events::EventDispatcher::instance().publish(notif);
            } else {
                events::ui::UIWindowClosedNotification notif;
                notif.entity = internal::toHandle(entity);
                notif.entityName = std::move(name);
                events::EventDispatcher::instance().publish(notif);
            }
        }
    }

    bool UIComponentService::addUIWindowComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::UIWindowComponent>()) return false;
        sceneEntity.addComponent<components::UIWindowComponent>();
        if (!sceneEntity.hasComponent<components::UIRectComponent>())
            sceneEntity.addComponent<components::UIRectComponent>();
        return true;
    }

    bool UIComponentService::removeUIWindowComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIWindowComponent>()) return false;

        // Drop a stale modal-stack entry before the component disappears
        auto& modalState = registry.ctx().emplace<components::UIModalState>();
        std::erase(modalState.modalStack, internal::fromHandle(entity));

        sceneEntity.removeComponent<components::UIWindowComponent>();
        return true;
    }

    bool UIComponentService::hasUIWindowComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIWindowComponent>();
    }

    std::optional<UIWindowData> UIComponentService::getUIWindowData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIWindowComponent>()) return std::nullopt;
        const auto& comp = sceneEntity.getComponent<components::UIWindowComponent>();
        UIWindowData data;
        data.title = comp.title;
        data.showTitleBar = comp.showTitleBar;
        data.titleBarHeight = comp.titleBarHeight;
        data.draggable = comp.draggable;
        data.closable = comp.closable;
        data.modal = comp.modal;
        data.backgroundColor = comp.backgroundColor;
        data.titleBarColor = comp.titleBarColor;
        data.titleTextColor = comp.titleTextColor;
        data.backdropColor = comp.backdropColor;
        data.fontRef = comp.fontRef;
        data.titleFontSize = comp.titleFontSize;
        return data;
    }

    bool UIComponentService::setUIWindowData(EntityHandle entity, const UIWindowData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIWindowComponent>()) return false;
        auto& comp = sceneEntity.getComponent<components::UIWindowComponent>();
        comp.title = data.title;
        comp.showTitleBar = data.showTitleBar;
        comp.titleBarHeight = data.titleBarHeight;
        comp.draggable = data.draggable;
        comp.closable = data.closable;
        comp.modal = data.modal;
        comp.backgroundColor = data.backgroundColor;
        comp.titleBarColor = data.titleBarColor;
        comp.titleTextColor = data.titleTextColor;
        comp.backdropColor = data.backdropColor;
        comp.fontRef = data.fontRef;
        comp.titleFontSize = data.titleFontSize;
        return true;
    }

    bool UIComponentService::openUIWindow(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* window = registry.try_get<components::UIWindowComponent>(handle);
        if (!window) return false;

        if (auto* name = registry.try_get<components::NameComponent>(handle))
            name->isActive = true;

        if (window->modal) {
            auto& modalState = registry.ctx().emplace<components::UIModalState>();
            std::erase(modalState.modalStack, handle);
            modalState.modalStack.push_back(handle);
        }

        publishWindowNotification(registry, handle, true);
        return true;
    }

    bool UIComponentService::closeUIWindow(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* window = registry.try_get<components::UIWindowComponent>(handle);
        if (!window) return false;

        if (auto* name = registry.try_get<components::NameComponent>(handle))
            name->isActive = false;
        window->isDraggingWindow = false;
        window->closeHovered = false;

        auto& modalState = registry.ctx().emplace<components::UIModalState>();
        std::erase(modalState.modalStack, handle);

        publishWindowNotification(registry, handle, false);
        return true;
    }

    bool UIComponentService::isUIWindowOpen(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        if (!registry.all_of<components::UIWindowComponent>(handle)) return false;
        return scene::Entity::isEffectivelyActive(registry, handle);
    }

    void UIComponentService::registerWindowHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::AddUIWindowComponentCommand>(
            [this](const events::ui::AddUIWindowComponentCommand& cmd) {
                return addUIWindowComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::RemoveUIWindowComponentCommand>(
            [this](const events::ui::RemoveUIWindowComponentCommand& cmd) {
                return removeUIWindowComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIWindowDataCommand>(
            [this](const events::ui::SetUIWindowDataCommand& cmd) {
                return setUIWindowData(cmd.entity, cmd.windowData);
            });
        dispatcher.registerCommandHandler<events::ui::OpenUIWindowCommand>(
            [this](const events::ui::OpenUIWindowCommand& cmd) {
                return openUIWindow(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::CloseUIWindowCommand>(
            [this](const events::ui::CloseUIWindowCommand& cmd) {
                return closeUIWindow(cmd.entity);
            });
        dispatcher.registerQueryHandler<events::ui::HasUIWindowComponentQuery>(
            [this](const events::ui::HasUIWindowComponentQuery& query) {
                return hasUIWindowComponent(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUIWindowDataQuery>(
            [this](const events::ui::GetUIWindowDataQuery& query) {
                return getUIWindowData(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::IsUIWindowOpenQuery>(
            [this](const events::ui::IsUIWindowOpenQuery& query) {
                return isUIWindowOpen(query.entity);
            });
    }

}
