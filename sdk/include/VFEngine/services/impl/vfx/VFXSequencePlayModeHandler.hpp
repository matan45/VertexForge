#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../data/VFXSequenceTypes.hpp"
#include "../../events/EventDispatcher.hpp"
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

        // Standalone auto-played combos, one per entity (mirrors VFXPlayModeHandler::activeVFXInstances).
        std::unordered_map<EntityHandle, VFXComboInstanceId, EntityHandle::Hash> autoPlayCombos;
        // Fire-and-forget combos spawned by animation triggers (auto-destroy on finish; tracked for teardown).
        std::vector<VFXComboInstanceId> triggeredCombos;
        bool sequenceActive = false;

        struct PendingTrigger
        {
            EntityHandle entity;
            std::string eventName;
        };
        std::mutex pendingMutex;
        std::vector<PendingTrigger> pendingTriggers;
        static constexpr size_t MAX_PENDING_TRIGGERS = 256;

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
        void drainPendingTriggers();
        void enterPlayMode();
        void exitPlayMode();
    };
}
