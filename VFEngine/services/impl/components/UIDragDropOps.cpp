#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"

namespace services {

    // ========== UI Draggable Operations ==========

    bool UIComponentService::addUIDraggableComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIDraggableComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIDraggableComponent>();

        // Auto-add UIRectComponent if missing (draggable needs rect for positioning)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIDraggableComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDraggableComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIDraggableComponent>();
        return true;
    }

    bool UIComponentService::hasUIDraggableComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIDraggableComponent>();
    }

    std::optional<UIDraggableData> UIComponentService::getUIDraggableData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDraggableComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIDraggableComponent>();

        UIDraggableData data;
        data.ghostOpacity = comp.ghostOpacity;
        data.ghostOffset = comp.ghostOffset;
        data.constrainToParent = comp.constrainToParent;
        data.dragTag = comp.dragTag;
        return data;
    }

    bool UIComponentService::setUIDraggableData(EntityHandle entity, const UIDraggableData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDraggableComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDraggableComponent>();
        comp.ghostOpacity = data.ghostOpacity;
        comp.ghostOffset = data.ghostOffset;
        comp.constrainToParent = data.constrainToParent;
        comp.dragTag = data.dragTag;
        return true;
    }

    // ========== UI DropTarget Operations ==========

    bool UIComponentService::addUIDropTargetComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));

        if (sceneEntity.hasComponent<components::UIDropTargetComponent>()) {
            return false;
        }

        sceneEntity.addComponent<components::UIDropTargetComponent>();

        // Auto-add UIRectComponent if missing (drop target needs rect for hit testing)
        if (!sceneEntity.hasComponent<components::UIRectComponent>()) {
            sceneEntity.addComponent<components::UIRectComponent>();
        }

        return true;
    }

    bool UIComponentService::removeUIDropTargetComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropTargetComponent>()) {
            return false;
        }

        sceneEntity.removeComponent<components::UIDropTargetComponent>();
        return true;
    }

    bool UIComponentService::hasUIDropTargetComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return sceneEntity.hasComponent<components::UIDropTargetComponent>();
    }

    std::optional<UIDropTargetData> UIComponentService::getUIDropTargetData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return std::nullopt;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropTargetComponent>()) {
            return std::nullopt;
        }

        const auto& comp = sceneEntity.getComponent<components::UIDropTargetComponent>();

        UIDropTargetData data;
        data.acceptTag = comp.acceptTag;
        data.highlightColor = comp.highlightColor;
        data.rejectColor = comp.rejectColor;
        data.interactable = comp.interactable;
        return data;
    }

    bool UIComponentService::setUIDropTargetData(EntityHandle entity, const UIDropTargetData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (!sceneEntity.hasComponent<components::UIDropTargetComponent>()) {
            return false;
        }

        auto& comp = sceneEntity.getComponent<components::UIDropTargetComponent>();
        comp.acceptTag = data.acceptTag;
        comp.highlightColor = data.highlightColor;
        comp.rejectColor = data.rejectColor;
        comp.interactable = data.interactable;
        return true;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerDragDropHandlers(events::EventDispatcher& dispatcher) {
        // Draggable commands
        dispatcher.registerCommandHandler<events::ui::AddUIDraggableComponentCommand>(
            [this](const events::ui::AddUIDraggableComponentCommand& cmd) {
                return addUIDraggableComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIDraggableComponentCommand>(
            [this](const events::ui::RemoveUIDraggableComponentCommand& cmd) {
                return removeUIDraggableComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDraggableDataCommand>(
            [this](const events::ui::SetUIDraggableDataCommand& cmd) {
                return setUIDraggableData(cmd.entity, cmd.draggableData);
            });

        // DropTarget commands
        dispatcher.registerCommandHandler<events::ui::AddUIDropTargetComponentCommand>(
            [this](const events::ui::AddUIDropTargetComponentCommand& cmd) {
                return addUIDropTargetComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::RemoveUIDropTargetComponentCommand>(
            [this](const events::ui::RemoveUIDropTargetComponentCommand& cmd) {
                return removeUIDropTargetComponent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::SetUIDropTargetDataCommand>(
            [this](const events::ui::SetUIDropTargetDataCommand& cmd) {
                return setUIDropTargetData(cmd.entity, cmd.dropTargetData);
            });

        // Draggable queries
        dispatcher.registerQueryHandler<events::ui::HasUIDraggableComponentQuery>(
            [this](const events::ui::HasUIDraggableComponentQuery& query) {
                return hasUIDraggableComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIDraggableDataQuery>(
            [this](const events::ui::GetUIDraggableDataQuery& query) {
                return getUIDraggableData(query.entity);
            });

        // Cancel drag command
        dispatcher.registerCommandHandler<events::ui::CancelDragCommand>(
            [](const events::ui::CancelDragCommand&) {
                auto& registry = scene::EntityRegistry::getRegistry();
                entt::entity dragEntity = components::UIDraggableComponent::activeDragEntity;
                if (dragEntity == entt::null || !registry.valid(dragEntity))
                    return false;
                if (registry.all_of<components::UIDraggableComponent>(dragEntity))
                    registry.get<components::UIDraggableComponent>(dragEntity).isDragging = false;
                components::UIDraggableComponent::activeDragEntity = entt::null;

                // Clear all drop target highlights
                auto dropView = registry.view<components::UIDropTargetComponent>();
                for (auto target : dropView)
                {
                    auto& tc = registry.get<components::UIDropTargetComponent>(target);
                    tc.isHighlighted = false;
                    tc.isRejected = false;
                }
                return true;
            });

        // DropTarget queries
        dispatcher.registerQueryHandler<events::ui::HasUIDropTargetComponentQuery>(
            [this](const events::ui::HasUIDropTargetComponentQuery& query) {
                return hasUIDropTargetComponent(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUIDropTargetDataQuery>(
            [this](const events::ui::GetUIDropTargetDataQuery& query) {
                return getUIDropTargetData(query.entity);
            });
    }

}
