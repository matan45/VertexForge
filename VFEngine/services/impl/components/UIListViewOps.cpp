#include "UIComponentService.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/ui/UIListViewEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include <algorithm>

namespace services {

    namespace {
        void setEntityActive(entt::registry& registry, entt::entity entity, bool active)
        {
            if (auto* name = registry.try_get<components::NameComponent>(entity))
                name->isActive = active;
        }

        void pruneInvalid(entt::registry& registry, std::vector<entt::entity>& entities)
        {
            std::erase_if(entities, [&registry](entt::entity e) { return !registry.valid(e); });
        }
    }

    bool UIComponentService::addUIListViewComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        scene::Entity sceneEntity(internal::fromHandle(entity));
        if (sceneEntity.hasComponent<components::UIListViewComponent>()) return false;
        sceneEntity.addComponent<components::UIListViewComponent>();
        if (!sceneEntity.hasComponent<components::UIRectComponent>())
            sceneEntity.addComponent<components::UIRectComponent>();
        // The layout group is the list's layout engine — items are positioned
        // by the existing per-frame layout pass.
        if (!sceneEntity.hasComponent<components::UILayoutGroupComponent>())
            sceneEntity.addComponent<components::UILayoutGroupComponent>();
        return true;
    }

    bool UIComponentService::removeUIListViewComponent(EntityHandle entity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* comp = registry.try_get<components::UIListViewComponent>(handle);
        if (!comp) return false;
        destroyUIListInstances(handle);
        registry.remove<components::UIListViewComponent>(handle);
        return true;
    }

    bool UIComponentService::hasUIListViewComponent(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        return registry.all_of<components::UIListViewComponent>(internal::fromHandle(entity));
    }

    std::optional<UIListViewData> UIComponentService::getUIListViewData(EntityHandle entity) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return std::nullopt;
        auto* comp = registry.try_get<components::UIListViewComponent>(internal::fromHandle(entity));
        if (!comp) return std::nullopt;
        UIListViewData data;
        data.itemTemplateRef = comp->itemTemplateRef;
        data.itemCount = comp->itemCount;
        data.selectable = comp->selectable;
        data.selectedTint = comp->selectedTint;
        data.selectedIndex = comp->selectedIndex;
        return data;
    }

    bool UIComponentService::setUIListViewData(EntityHandle entity, const UIListViewData& data) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* comp = registry.try_get<components::UIListViewComponent>(handle);
        if (!comp) return false;

        bool templateChanged = comp->itemTemplateRef != data.itemTemplateRef;
        comp->itemTemplateRef = data.itemTemplateRef;
        comp->itemCount = data.itemCount;
        comp->selectable = data.selectable;
        comp->selectedTint = data.selectedTint;

        if (templateChanged)
            destroyUIListInstances(handle);
        return reconcileUIListView(handle);
    }

    // Destroys all instances + pooled spares (template change / component removal)
    void UIComponentService::destroyUIListInstances(entt::entity listEntity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto* comp = registry.try_get<components::UIListViewComponent>(listEntity);
        if (!comp) return;

        auto& dispatcher = events::EventDispatcher::instance();
        auto destroyAll = [&](std::vector<entt::entity>& entities) {
            for (entt::entity e : entities)
            {
                if (!registry.valid(e)) continue;
                events::scene::DeleteEntityCommand cmd;
                cmd.entity = internal::toHandle(e);
                dispatcher.execute(cmd);
            }
            entities.clear();
        };
        destroyAll(comp->itemInstances);
        destroyAll(comp->pool);
        comp->selectedIndex = -1;
        comp->needsReconcile = true;
    }

    // Brings the instance set in line with itemCount: shrink deactivates into
    // the pool; growth reuses the pool first, then instantiates the .vfPrefab
    // item template under the list entity.
    bool UIComponentService::reconcileUIListView(entt::entity listEntity) {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto* comp = registry.try_get<components::UIListViewComponent>(listEntity);
        if (!comp) return false;

        pruneInvalid(registry, comp->itemInstances);
        pruneInvalid(registry, comp->pool);

        std::string templatePath;
        if (comp->itemTemplateRef.isValid())
            templatePath = comp->itemTemplateRef.resolve();

        size_t target = templatePath.empty()
            ? 0
            : static_cast<size_t>(std::max(0, comp->itemCount));

        // Shrink: deactivate surplus into the pool (inactive children are
        // skipped by layout, rendering and serialization-by-marker).
        while (comp->itemInstances.size() > target)
        {
            entt::entity item = comp->itemInstances.back();
            comp->itemInstances.pop_back();
            setEntityActive(registry, item, false);
            comp->pool.push_back(item);
        }

        // Grow: pool first, then instantiate the template
        auto& dispatcher = events::EventDispatcher::instance();
        while (comp->itemInstances.size() < target)
        {
            entt::entity item = entt::null;
            if (!comp->pool.empty())
            {
                item = comp->pool.back();
                comp->pool.pop_back();
                setEntityActive(registry, item, true);
            }
            else
            {
                events::scene::LoadPrefabCommand cmd;
                cmd.filePath = templatePath;
                cmd.parent = internal::toHandle(listEntity);
                auto result = dispatcher.execute(cmd);
                if (!result.has_value() || !internal::isValidHandle(*result, registry))
                {
                    vfLogWarning("UIListView: failed to instantiate item template '{}'", templatePath);
                    break;
                }
                item = internal::fromHandle(*result);
                setEntityActive(registry, item, true);
            }

            auto& marker = registry.emplace_or_replace<components::UIListItemComponent>(item);
            marker.listView = listEntity;
            comp->itemInstances.push_back(item);
        }

        // Refresh marker indices
        for (size_t i = 0; i < comp->itemInstances.size(); ++i)
        {
            if (auto* marker = registry.try_get<components::UIListItemComponent>(comp->itemInstances[i]))
                marker->index = static_cast<int>(i);
        }

        if (comp->selectedIndex >= static_cast<int>(comp->itemInstances.size()))
            comp->selectedIndex = -1;

        comp->needsReconcile = false;
        return true;
    }

    bool UIComponentService::setUIListItemCount(EntityHandle entity, int itemCount) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* comp = registry.try_get<components::UIListViewComponent>(handle);
        if (!comp) return false;
        comp->itemCount = std::max(0, itemCount);
        return reconcileUIListView(handle);
    }

    bool UIComponentService::setUIListItemTemplate(EntityHandle entity, const std::string& templatePath) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* comp = registry.try_get<components::UIListViewComponent>(handle);
        if (!comp) return false;

        asset::AssetRef newRef = templatePath.empty()
            ? asset::AssetRef::invalid()
            : asset::AssetRef::fromPath(templatePath);
        if (!templatePath.empty() && !newRef.isValid())
        {
            vfLogWarning("UIListView: item template not found: {}", templatePath);
            return false;
        }

        if (comp->itemTemplateRef != newRef)
        {
            comp->itemTemplateRef = newRef;
            destroyUIListInstances(handle);
        }
        return reconcileUIListView(handle);
    }

    EntityHandle UIComponentService::getUIListItem(EntityHandle entity, int index) const {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return EntityHandle::invalid();
        auto* comp = registry.try_get<components::UIListViewComponent>(internal::fromHandle(entity));
        if (!comp) return EntityHandle::invalid();
        if (index < 0 || index >= static_cast<int>(comp->itemInstances.size()))
            return EntityHandle::invalid();
        entt::entity item = comp->itemInstances[static_cast<size_t>(index)];
        if (!registry.valid(item)) return EntityHandle::invalid();
        return internal::toHandle(item);
    }

    bool UIComponentService::setUIListSelectedIndex(EntityHandle entity, int selectedIndex) {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry)) return false;
        entt::entity handle = internal::fromHandle(entity);
        auto* comp = registry.try_get<components::UIListViewComponent>(handle);
        if (!comp) return false;

        int clamped = (selectedIndex >= 0 && selectedIndex < static_cast<int>(comp->itemInstances.size()))
            ? selectedIndex : -1;
        if (clamped == comp->selectedIndex) return true;

        int previous = comp->selectedIndex;
        comp->selectedIndex = clamped;

        std::string name;
        if (auto* nameComp = registry.try_get<components::NameComponent>(handle))
            name = nameComp->name;

        events::ui::UIListSelectionChangedNotification notif;
        notif.entity = entity;
        notif.entityName = std::move(name);
        notif.previousIndex = previous;
        notif.newIndex = clamped;
        events::EventDispatcher::instance().publish(notif);
        return true;
    }

    void UIComponentService::reconcileAllUIListViews() {
        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto entity : registry.view<components::UIListViewComponent>())
        {
            reconcileUIListView(entity);
        }
    }

    void UIComponentService::registerListViewHandlers(events::EventDispatcher& dispatcher) {
        dispatcher.registerCommandHandler<events::ui::AddUIListViewComponentCommand>(
            [this](const events::ui::AddUIListViewComponentCommand& cmd) {
                return addUIListViewComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::RemoveUIListViewComponentCommand>(
            [this](const events::ui::RemoveUIListViewComponentCommand& cmd) {
                return removeUIListViewComponent(cmd.entity);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIListViewDataCommand>(
            [this](const events::ui::SetUIListViewDataCommand& cmd) {
                return setUIListViewData(cmd.entity, cmd.listViewData);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIListItemCountCommand>(
            [this](const events::ui::SetUIListItemCountCommand& cmd) {
                return setUIListItemCount(cmd.entity, cmd.itemCount);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIListItemTemplateCommand>(
            [this](const events::ui::SetUIListItemTemplateCommand& cmd) {
                return setUIListItemTemplate(cmd.entity, cmd.templatePath);
            });
        dispatcher.registerCommandHandler<events::ui::SetUIListSelectedIndexCommand>(
            [this](const events::ui::SetUIListSelectedIndexCommand& cmd) {
                return setUIListSelectedIndex(cmd.entity, cmd.selectedIndex);
            });
        dispatcher.registerQueryHandler<events::ui::HasUIListViewComponentQuery>(
            [this](const events::ui::HasUIListViewComponentQuery& query) {
                return hasUIListViewComponent(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUIListViewDataQuery>(
            [this](const events::ui::GetUIListViewDataQuery& query) {
                return getUIListViewData(query.entity);
            });
        dispatcher.registerQueryHandler<events::ui::GetUIListItemQuery>(
            [this](const events::ui::GetUIListItemQuery& query) {
                return getUIListItem(query.entity, query.index);
            });

        // Item instances are never serialized — rebuild them after scene load
        listViewSceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&) {
                reconcileAllUIListViews();
            });
    }

}
