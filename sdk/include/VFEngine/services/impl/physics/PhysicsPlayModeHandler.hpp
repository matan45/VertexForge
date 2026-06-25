#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include <unordered_set>
#include <unordered_map>
#include <vector>
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
        ::events::SubscriptionToken rigidBodyAddedToken;
        ::events::SubscriptionToken rigidBodyRemovedToken;
        ::events::SubscriptionToken prefabInstantiatedToken;
        ::events::SubscriptionToken entityDeletedToken;
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
        // VK-1437: idempotent per-entity physics-animation init over an explicit candidate set. Skips
        // entities already initialized (createPhysicsAnimation is NOT idempotent) and entities missing
        // the required components. Used both by the Play-entry full pass and by mid-Play prefab spawns.
        void initializePhysicsAnimationsFor(const std::vector<EntityHandle>& candidates);
        void cleanupPhysicsAnimations();
        void initializeCharacterControllers();
        void cleanupCharacterControllers();
    };
}
