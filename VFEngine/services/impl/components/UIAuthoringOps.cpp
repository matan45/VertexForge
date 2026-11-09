#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "ui/UIRectMath.hpp"
#include "ui/UICheckboxToggle.hpp"
#include "ui/UICompoundValidation.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIEvents.hpp"
#include <string>
#include <utility>
#include <vector>

// VK-1659: the UI authoring events of the MCP UI tools (events/ui/UIAuthoringEvents.hpp).

namespace services {

    namespace {
        std::string entityName(const entt::registry& registry, entt::entity entity)
        {
            const auto* name = registry.try_get<components::NameComponent>(entity);
            return name ? name->name : std::string();
        }
    }

    // ========== Click simulation ==========

    UIClickResult UIComponentService::simulateUIClick(EntityHandle entity) {
        UIClickResult result;
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            result.status = UIClickStatus::NotFound;
            return result;
        }

        const entt::entity clicked = internal::fromHandle(entity);
        entt::entity target = entt::null;
        bool viaLabel = false;
        if (registry.all_of<components::UIButtonComponent>(clicked)) {
            target = clicked;
            result.kind = UIClickKind::Button;
        }
        else if (registry.all_of<components::UICheckboxComponent>(clicked)) {
            target = clicked;
            result.kind = UIClickKind::Checkbox;
        }
        else if (registry.all_of<components::UILabelComponent>(clicked)) {
            // A labelToggle checkbox also takes clicks on its child labels (UIInteractionSystem PHASE 1).
            const auto* parent = registry.try_get<components::ParentComponent>(clicked);
            if (parent && parent->parent != entt::null && registry.valid(parent->parent)) {
                const auto* checkbox = registry.try_get<components::UICheckboxComponent>(parent->parent);
                if (checkbox && checkbox->labelToggle) {
                    target = parent->parent;
                    viaLabel = true;
                    result.kind = UIClickKind::Checkbox;
                }
            }
        }

        if (target == entt::null) {
            result.status = UIClickStatus::NotClickable;
            return result;
        }

        result.target = internal::toHandle(target);
        result.targetName = entityName(registry, target);

        // The checks of the runtime hit test (UIInteractionSystem findHoveredWidget): a rect to hit (the label's
        // too when the click goes through it), interactable, effectively active, inside a canvas, and not under
        // a modal window other than its own.
        if (!registry.all_of<components::UIRectComponent>(target) ||
            (viaLabel && !registry.all_of<components::UIRectComponent>(clicked))) {
            result.status = UIClickStatus::NoRect;
            return result;
        }

        const bool interactable = result.kind == UIClickKind::Button
            ? registry.get<components::UIButtonComponent>(target).interactable
            : registry.get<components::UICheckboxComponent>(target).interactable;
        if (!interactable) {
            result.status = UIClickStatus::NotInteractable;
            return result;
        }

        if (!scene::Entity::isEffectivelyActive(registry, target)) {
            result.status = UIClickStatus::Inactive;
            return result;
        }

        if (utilities::ui::findCanvasForEntity(registry, target) == nullptr) {
            result.status = UIClickStatus::NoCanvas;
            return result;
        }

        if (!utilities::ui::isInteractionAllowed(registry, target)) {
            result.status = UIClickStatus::BlockedByModal;
            return result;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        if (result.kind == UIClickKind::Button) {
            const EntityHandle handle = result.target;
            const std::string name = result.targetName;

            // A real click: Pressed on the press frame, then Released and Clicked on the release frame.
            events::ui::UIButtonPressedNotification pressedNotif;
            pressedNotif.entity = handle;
            pressedNotif.entityName = name;
            dispatcher.publish(pressedNotif);

            events::ui::UIButtonReleasedNotification releasedNotif;
            releasedNotif.entity = handle;
            releasedNotif.entityName = name;
            dispatcher.publish(releasedNotif);

            events::ui::UIButtonClickedNotification clickedNotif;
            clickedNotif.entity = handle;
            clickedNotif.entityName = name;
            dispatcher.publish(clickedNotif);
        }
        else {
            utilities::ui::applyCheckboxClick(registry, target,
                [&](entt::entity toggledEntity, bool newChecked, bool previousChecked) {
                    UICheckboxToggleData toggle;
                    toggle.entity = internal::toHandle(toggledEntity);
                    toggle.name = entityName(registry, toggledEntity);
                    toggle.newChecked = newChecked;
                    toggle.previousChecked = previousChecked;

                    events::ui::UICheckboxToggledNotification notif;
                    notif.entity = toggle.entity;
                    notif.entityName = toggle.name;
                    notif.newCheckedState = newChecked;
                    notif.previousCheckedState = previousChecked;

                    result.toggles.push_back(std::move(toggle));
                    dispatcher.publish(notif);
                });
        }

        result.status = UIClickStatus::Clicked;
        return result;
    }

    // ========== List view items after instantiation ==========

    int UIComponentService::reconcileUIListViewsInSubtree(entt::entity root) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (root == entt::null || !registry.valid(root)) {
            return 0;
        }

        // Collect first: reconciling instantiates item templates (new entities, maybe new list views) under
        // the lists being visited. Those new lists are built by the instantiation itself; reconcileUIListView
        // stops a template that holds a list of itself, and nesting deeper than four templates.
        std::vector<entt::entity> listViews;
        std::vector<entt::entity> pending{root};
        while (!pending.empty()) {
            const entt::entity current = pending.back();
            pending.pop_back();
            if (current == entt::null || !registry.valid(current)) {
                continue;
            }
            if (registry.all_of<components::UIListViewComponent>(current)) {
                listViews.push_back(current);
            }
            if (const auto* children = registry.try_get<components::ChildrenComponent>(current)) {
                // Reversed so the walk visits the children in hierarchy order.
                for (auto it = children->children.rbegin(); it != children->children.rend(); ++it) {
                    pending.push_back(*it);
                }
            }
        }

        int reconciled = 0;
        for (entt::entity listEntity : listViews) {
            if (registry.valid(listEntity) && reconcileUIListView(listEntity)) {
                ++reconciled;
            }
        }
        return reconciled;
    }

    // ========== Queries ==========

    bool UIComponentService::isUIListItem(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }
        return registry.all_of<components::UIListItemComponent>(internal::fromHandle(entity));
    }

    UICompoundStatusData UIComponentService::getUICompoundStatus(EntityHandle entity) const {
        UICompoundStatusData data;
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return data;
        }

        ui_validation::CompoundWidgetStatus status =
            ui_validation::validateCompoundWidget(registry, internal::fromHandle(entity));
        data.ok = status.ok;
        data.errors = std::move(status.errors);
        data.warnings = std::move(status.warnings);
        return data;
    }

    bool UIComponentService::isInUIPreviewSandbox(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) {
            return false;
        }
        // The builder tags only the sandbox canvas root, so walk up to it.
        entt::entity current = internal::fromHandle(entity);
        while (current != entt::null && registry.valid(current)) {
            if (registry.all_of<components::UIPreviewTagComponent>(current)) {
                return true;
            }
            const auto* parent = registry.try_get<components::ParentComponent>(current);
            current = parent ? parent->parent : entt::null;
        }
        return false;
    }

    // ========== Event Handler Registration ==========

    void UIComponentService::registerAuthoringHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::SimulateUIClickCommand>(
            [this](const events::ui::SimulateUIClickCommand& cmd) {
                return simulateUIClick(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::ui::ReconcileUIListViewsCommand>(
            [this](const events::ui::ReconcileUIListViewsCommand& cmd) {
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!internal::isValidHandle(cmd.root, registry)) {
                    return 0;
                }
                return reconcileUIListViewsInSubtree(internal::fromHandle(cmd.root));
            });

        dispatcher.registerQueryHandler<events::ui::IsUIListItemQuery>(
            [this](const events::ui::IsUIListItemQuery& query) {
                return isUIListItem(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::GetUICompoundStatusQuery>(
            [this](const events::ui::GetUICompoundStatusQuery& query) {
                return getUICompoundStatus(query.entity);
            });

        dispatcher.registerQueryHandler<events::ui::IsInUIPreviewSandboxQuery>(
            [this](const events::ui::IsInUIPreviewSandboxQuery& query) {
                return isInUIPreviewSandbox(query.entity);
            });
    }

}
