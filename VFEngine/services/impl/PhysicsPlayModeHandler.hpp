#pragma once

#include "../data/EntityHandle.hpp"
#include "../data/EditorMode.hpp"
#include "../events/EventDispatcher.hpp"
#include "../providers/IPhysicsProvider.hpp"
#include <unordered_set>

namespace services
{
    class WaterService;

    class PhysicsPlayModeHandler
    {
    private:
        IPhysicsProvider* physicsProvider = nullptr;
        WaterService* waterService = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
        std::unordered_set<EntityHandle, EntityHandle::Hash> activePhysicsBodies;
        bool physicsActive = false;

    public:
        explicit PhysicsPlayModeHandler(IPhysicsProvider* physicsProvider);
        ~PhysicsPlayModeHandler();

        PhysicsPlayModeHandler(const PhysicsPlayModeHandler&) = delete;
        PhysicsPlayModeHandler& operator=(const PhysicsPlayModeHandler&) = delete;

        void setWaterService(WaterService* service) { waterService = service; }

        void subscribeToEvents();
        void unsubscribeFromEvents();

        void update(float deltaTime);

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void enterPlayMode();
        void exitPlayMode();
        void syncTransformsFromPhysics();
    };
}
