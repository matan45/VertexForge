#pragma once
#include "../../data/EntityHandle.hpp"
#include <memory>
#include <optional>
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
    class EntityStateService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        std::optional<EntityHandle> selectedEntity;

    public:
        explicit EntityStateService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        // Selection state
        void setSelectedEntity(std::optional<EntityHandle> entity);
        std::optional<EntityHandle> getSelectedEntity() const;
        void clearSelection();

        // Entity metadata
        void setEntityName(EntityHandle entity, const std::string& name);
        std::string getEntityName(EntityHandle entity) const;
        void setEntityActive(EntityHandle entity, bool isActive);

        // Static state
        bool setEntityStatic(EntityHandle entity, bool isStatic);
        bool isEntityStatic(EntityHandle entity) const;
    };
}
