#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"

namespace services {

    // ========== UI Scroll Operations ==========

    bool UIComponentService::addUIScrollComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIScrollComponent>();

        // Auto-add UIRectComponent if missing (scroll needs rect for viewport bounds)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIScrollComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIScrollComponent>();
        return true;
    }

    bool UIComponentService::hasUIScrollComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIScrollComponent>();
    }

    std::optional<UIScrollData> UIComponentService::getUIScrollData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();

        UIScrollData data;
        data.horizontalScrollEnabled = comp.horizontalScrollEnabled;
        data.verticalScrollEnabled = comp.verticalScrollEnabled;
        data.horizontalScrollbarVisibility = static_cast<uint8_t>(comp.horizontalScrollbarVisibility);
        data.verticalScrollbarVisibility = static_cast<uint8_t>(comp.verticalScrollbarVisibility);
        data.scrollSensitivity = comp.scrollSensitivity;
        return data;
    }

    bool UIComponentService::setUIScrollData(EntityHandle entity, const UIScrollData& scrollData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        comp.horizontalScrollEnabled = scrollData.horizontalScrollEnabled;
        comp.verticalScrollEnabled = scrollData.verticalScrollEnabled;
        comp.horizontalScrollbarVisibility = static_cast<components::ScrollbarVisibility>(scrollData.horizontalScrollbarVisibility);
        comp.verticalScrollbarVisibility = static_cast<components::ScrollbarVisibility>(scrollData.verticalScrollbarVisibility);
        comp.scrollSensitivity = scrollData.scrollSensitivity;
        return true;
    }

    bool UIComponentService::setScrollOffset(EntityHandle entity, const glm::vec2& offset) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        comp.scrollOffset = offset;
        return true;
    }

    std::optional<glm::vec2> UIComponentService::getScrollOffset(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIScrollComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIScrollComponent>();
        return comp.scrollOffset;
    }

    // ========== UI Layout Group Operations ==========

    bool UIComponentService::addUILayoutGroupComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UILayoutGroupComponent>();

        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        if (!sceneEntity.hasComponent<components::ChildrenComponent>()) {
            sceneEntity.addComponent<components::ChildrenComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUILayoutGroupComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UILayoutGroupComponent>();
        return true;
    }

    bool UIComponentService::hasUILayoutGroupComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UILayoutGroupComponent>();
    }

    std::optional<UILayoutGroupData> UIComponentService::getUILayoutGroupData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UILayoutGroupComponent>();

        UILayoutGroupData data;
        data.direction = static_cast<uint8_t>(comp.direction);
        data.spacing = comp.spacing;
        data.padding = comp.padding;
        data.childAlignment = static_cast<uint8_t>(comp.childAlignment);
        data.constraintCount = comp.constraintCount;
        return data;
    }

    bool UIComponentService::setUILayoutGroupData(EntityHandle entity, const UILayoutGroupData& layoutGroupData) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UILayoutGroupComponent>();
        comp.direction = static_cast<components::LayoutDirection>(layoutGroupData.direction);
        comp.spacing = layoutGroupData.spacing;
        comp.padding = layoutGroupData.padding;
        comp.childAlignment = static_cast<components::ChildAlignment>(layoutGroupData.childAlignment);
        comp.constraintCount = std::max(1, layoutGroupData.constraintCount);
        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerScrollLayoutHandlers(events::EventDispatcher& dispatcher) {
        // Scroll commands
        dispatcher.registerCommandHandler<events::ui::AddUIScrollComponentCommand>(
            [this](const events::ui::AddUIScrollComponentCommand& cmd) {
                return addUIScrollComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIScrollComponentCommand>(
            [this](const events::ui::RemoveUIScrollComponentCommand& cmd) {
                return removeUIScrollComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIScrollDataCommand>(
            [this](const events::ui::SetUIScrollDataCommand& cmd) {
                return setUIScrollData(cmd.entity, cmd.scrollData);
            });

        dispatcher.registerCommandHandler<events::ui::SetScrollOffsetCommand>(
            [this](const events::ui::SetScrollOffsetCommand& cmd) {
                return setScrollOffset(cmd.entity, cmd.offset);
            });

        dispatcher.registerQueryHandler<events::ui::HasUIScrollComponentQuery>(
            [this](const events::ui::HasUIScrollComponentQuery& query) {
                return hasUIScrollComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIScrollDataQuery>(
            [this](const events::ui::GetUIScrollDataQuery& query) {
                return getUIScrollData(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetScrollOffsetQuery>(
            [this](const events::ui::GetScrollOffsetQuery& query) {
                return getScrollOffset(query.entity);
            });

        // Layout Group commands
        dispatcher.registerCommandHandler<events::ui::AddUILayoutGroupComponentCommand>(
            [this](const events::ui::AddUILayoutGroupComponentCommand& cmd) {
                return addUILayoutGroupComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUILayoutGroupComponentCommand>(
            [this](const events::ui::RemoveUILayoutGroupComponentCommand& cmd) {
                return removeUILayoutGroupComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUILayoutGroupDataCommand>(
            [this](const events::ui::SetUILayoutGroupDataCommand& cmd) {
                return setUILayoutGroupData(cmd.entity, cmd.layoutGroupData);
            });

        dispatcher.registerQueryHandler<events::ui::HasUILayoutGroupComponentQuery>(
            [this](const events::ui::HasUILayoutGroupComponentQuery& query) {
                return hasUILayoutGroupComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUILayoutGroupDataQuery>(
            [this](const events::ui::GetUILayoutGroupDataQuery& query) {
                return getUILayoutGroupData(query.entity);
            });
    }

}
