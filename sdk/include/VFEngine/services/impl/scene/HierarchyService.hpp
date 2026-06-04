#pragma once
#include "../../data/EntityHandle.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
    class Entity;
}

namespace events
{
    class EventDispatcher;
}

namespace services
{
    class HierarchyService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        void collectEntityAndDescendants(scene::Entity& entity, std::vector<EntityHandle>& outHandles) const;

    public:
        explicit HierarchyService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        EntityHandle createEntity(const std::string& name, std::optional<EntityHandle> parent = std::nullopt);
        bool deleteEntity(EntityHandle entity, bool deleteChildren = true);
        EntityHandle duplicateEntity(EntityHandle entity);
        bool reparentEntity(EntityHandle entity, EntityHandle newParent);
        bool moveEntity(EntityHandle entity, EntityHandle targetParent, int insertIndex);
        EntityHandle getRoot() const;
        std::vector<EntityHandle> getChildren(EntityHandle entity) const;
    };
}
