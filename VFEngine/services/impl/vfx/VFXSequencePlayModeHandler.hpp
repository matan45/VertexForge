#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/VFXSequenceTypes.hpp"
#include "../../events/EventDispatcher.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace services
{
    // Drives VFX combos in editor play mode (VK-1425), analogous to VFXPlayModeHandler:
    //  - on enter-play, auto-plays VFXSequenceComponents flagged autoPlay;
    //  - bridges authored animation notify events to combo spawns via the component's
    //    eventName -> sequence trigger map. AnimationEventFiredNotification is published on a
    //    worker/Render task, so triggers are queued under a mutex and drained on the VFX task
    //    (one-frame latency); combos are then created/played through the vfxsequence command API.
    class VFXSequencePlayModeHandler
    {
    private:
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken transformChangedToken;
        ::events::SubscriptionToken animationEventToken;
        ::events::SubscriptionToken prefabInstantiatedToken; // VK-1438: mid-Play prefab spawn autoplay
        ::events::SubscriptionToken entityDeletedToken;      // VK-1438: mid-Play entity-delete cleanup
        ::events::SubscriptionToken sceneLoadedToken;        // Runtime deferred startup scene autoplay

        // Standalone auto-played combos, one per entity (mirrors VFXPlayModeHandler::activeVFXInstances).
        std::unordered_map<EntityHandle, VFXComboInstanceId, EntityHandle::Hash> autoPlayCombos;
        // Fire-and-forget combos spawned by animation triggers (auto-destroy on finish; tracked for teardown).
        std::vector<VFXComboInstanceId> triggeredCombos;
        // Read by the off-thread AnimationEvent/Transform subscribers, written on the main thread
        // (enter/exit play) — atomic to avoid a data race.
        std::atomic<bool> sequenceActive{false};

        struct PendingTrigger
        {
            EntityHandle entity;
            std::string eventName;
        };
        std::mutex pendingMutex;
        std::vector<PendingTrigger> pendingTriggers;
        static constexpr size_t MAX_PENDING_TRIGGERS = 256;
        int sceneRescanFrames = 0; // guarded by pendingMutex
        static constexpr int kSceneLoadRescanFrames = 3;

        // VK-1438: prefab roots instantiated mid-Play, drained in update() so world transforms are
        // settled before combos are created. Guarded by pendingMutex (PrefabInstantiatedNotification
        // may arrive off the update() thread, like the trigger queue).
        std::vector<EntityHandle> pendingPrefabRoots;

    public:
        VFXSequencePlayModeHandler() = default;
        ~VFXSequencePlayModeHandler();

        VFXSequencePlayModeHandler(const VFXSequencePlayModeHandler&) = delete;
        VFXSequencePlayModeHandler& operator=(const VFXSequencePlayModeHandler&) = delete;

        void subscribeToEvents();
        void unsubscribeFromEvents();

        void update(float deltaTime);

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void onTransformChanged(EntityHandle entity);
        void onAnimationEvent(EntityHandle entity, const std::string& eventName);
        void onPrefabInstantiated(EntityHandle root);
        void onEntityDeleted(EntityHandle entity);
        void drainPendingTriggers();
        void drainPendingPrefabRoots();
        void scanAndAutoPlayCombos();
        void tryAutoPlayCombo(EntityHandle handle);
        void enterPlayMode();
        void exitPlayMode();
    };
}
