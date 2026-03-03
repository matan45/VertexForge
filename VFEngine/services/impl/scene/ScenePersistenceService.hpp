#pragma once
#include "../../data/EntityHandle.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/AudioTypes.hpp"
#include "types/RenderSettings.hpp"
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
        std::optional<std::string> pendingLoadPath;

    public:
        explicit ScenePersistenceService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                         EntityStateService* entityStateService);

        void registerEventHandlers(events::EventDispatcher& dispatcher);

        void update();

        bool newScene();
        bool saveScene(const std::string& filePath);
        bool loadScene(const std::string& filePath);
        bool savePrefab(EntityHandle entity, const std::string& filePath);
        std::optional<EntityHandle> loadPrefab(const std::string& filePath,
                                               std::optional<EntityHandle> parent = std::nullopt);

        types::PhysicsSettings getPhysicsSettings() const;
        bool setPhysicsSettings(const types::PhysicsSettings& settings);

        types::AudioSettings getAudioSettings() const;
        bool setAudioSettings(const types::AudioSettings& settings);

        types::RenderSettings getRenderSettings() const;
        bool setRenderSettings(const types::RenderSettings& settings);

    private:
        void performDeferredLoad(const std::string& filePath);
    };
}
