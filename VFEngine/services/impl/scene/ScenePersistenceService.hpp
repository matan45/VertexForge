#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/types/PhysicsTypes.hpp"
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
    class EntityStateService;

    class ScenePersistenceService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        EntityStateService* entityStateService;

    public:
        ScenePersistenceService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                EntityStateService* entityStateService);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);
        bool savePrefab(EntityHandle entity, const std::string& filePath);
        std::optional<EntityHandle> loadPrefab(const std::string& filePath,
                                               std::optional<EntityHandle> parent = std::nullopt);

        // Physics settings (stored at scene level)
        types::PhysicsSettings getPhysicsSettings() const;
        bool setPhysicsSettings(const types::PhysicsSettings& settings);
    };
}
