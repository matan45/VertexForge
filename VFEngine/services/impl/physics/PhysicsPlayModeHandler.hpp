#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include <unordered_set>
#include <unordered_map>
#include <glm/glm.hpp>

namespace services
{
    class OceanService;

    class PhysicsPlayModeHandler
    {
    private:
        IPhysicsProvider* physicsProvider = nullptr;
        OceanService* oceanService = nullptr;
        std::function<void(float)> scriptFixedUpdateCallback;
        ::events::SubscriptionToken editorModeChangedToken;
        std::unordered_set<EntityHandle, EntityHandle::Hash> activePhysicsBodies;
        bool physicsActive = false;

        std::unordered_set<EntityHandle, EntityHandle::Hash> activePhysicsAnimationEntities;
        std::unordered_set<EntityHandle, EntityHandle::Hash> activeCharacterControllers;
        std::unordered_map<EntityHandle, glm::vec3, EntityHandle::Hash> rootMotionLastSyncPos;
    public:
        explicit PhysicsPlayModeHandler(IPhysicsProvider* physicsProvider);
        ~PhysicsPlayModeHandler();

        PhysicsPlayModeHandler(const PhysicsPlayModeHandler&) = delete;
        PhysicsPlayModeHandler& operator=(const PhysicsPlayModeHandler&) = delete;

        void setOceanService(OceanService* service) { oceanService = service; }
        void setScriptFixedUpdateCallback(std::function<void(float)> callback) { scriptFixedUpdateCallback = std::move(callback); }

        void subscribeToEvents();
        void unsubscribeFromEvents();

        void update(float deltaTime);
        void kickUpdate(float deltaTime);
        void syncUpdate(float deltaTime);

    private:
        
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void enterPlayMode();
        void exitPlayMode();
        void syncTransformsFromPhysics();
        void syncRootMotionEntity(EntityHandle handle);
        void syncStandardPhysicsEntity(EntityHandle handle);
        void initializePhysicsAnimations();
        void cleanupPhysicsAnimations();
        void initializeCharacterControllers();
        void cleanupCharacterControllers();
    };
}
