#pragma once

#include "../data/EntityHandle.hpp"
#include "../data/EditorMode.hpp"
#include "../events/EventDispatcher.hpp"
#include "../providers/IPhysicsProvider.hpp"
#include <unordered_set>

namespace services
{
    class PhysicsPlayModeHandler
    {
    public:
        explicit PhysicsPlayModeHandler(IPhysicsProvider* physicsProvider);
        ~PhysicsPlayModeHandler();

        PhysicsPlayModeHandler(const PhysicsPlayModeHandler&) = delete;
        PhysicsPlayModeHandler& operator=(const PhysicsPlayModeHandler&) = delete;

        void subscribeToEvents();
        void unsubscribeFromEvents();

        // Called each frame during play mode to step physics and sync transforms
        void update(float deltaTime);

        bool isPhysicsActive() const { return physicsActive; }

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void enterPlayMode();
        void exitPlayMode();
        void syncTransformsFromPhysics();

        IPhysicsProvider* physicsProvider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
        std::unordered_set<EntityHandle, EntityHandle::Hash> activePhysicsBodies;
        bool physicsActive = false;
    };
}
