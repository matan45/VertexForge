#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <optional>
#include <vector>
#include <string>

namespace scene
{
    class SceneGraphSystem;
}

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class EntityQueryService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        EntityData buildEntityData(entt::entity entity) const;
        void collectHierarchy(entt::entity entity, std::vector<EntityData>& entities) const;

    public:
        explicit EntityQueryService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        std::optional<EntityData> getEntity(EntityHandle handle) const;
        std::optional<EntityHandle> findEntityByName(const std::string& name) const;
        std::vector<EntityHandle> findEntitiesByName(const std::string& name) const;
        std::vector<EntityHandle> getEntitiesWithComponent(ComponentTypeId type) const;
        SceneHierarchyData getSceneHierarchy() const;
        bool hasComponent(EntityHandle entity, ComponentTypeId type) const;
        std::vector<ComponentTypeId> getComponentTypes(EntityHandle entity) const;
        EntityHandle getRoot() const;
    };
}
