#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/VFXTypes.hpp"
#include "../../events/EventTypes.hpp"
#include <vfx/VFXParameterRegistry.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    // Declared in events/vfx/VFXEditPreviewEvents.hpp, which is not included here: a services::events
    // header in this one would shadow ::events for every includer inside namespace services.
    struct VFXEditPreviewStatus;
    struct VFXEditPreviewResult;

    // VK-1657 — edit-mode preview of entities' VFX components (Editor only; the
    // services::events::vfxeditpreview commands, MCP vfx_play / vfx_stop). A preview is a persistent
    // renderer instance created from the component's ref, loop flag and per-entity overrides;
    // update() keeps it in line with later component and transform edits. It never writes the
    // component's runtimeInstanceId / isPlaying (Play-mode state).
    //
    // Threads: start/stop/list run on the main thread (MCP drain, ImGui), update() in the VFX frame
    // task, notifications on their publisher's thread. Scene clear and entity deletion only queue
    // stops; renderer commands go out from start/stop/update and the Edit->Play teardown, never
    // while the render thread may be busy.
    class VFXEditPreviewHandler
    {
    public:
        static constexpr size_t kMaxPreviews = 64;

        VFXEditPreviewHandler() = default;
        ~VFXEditPreviewHandler();

        VFXEditPreviewHandler(const VFXEditPreviewHandler&) = delete;
        VFXEditPreviewHandler& operator=(const VFXEditPreviewHandler&) = delete;

        void registerEventHandlers();
        void unregisterEventHandlers();
        void subscribeToEvents();
        void unsubscribeFromEvents();

        // VFX frame task, edit mode only: applies queued stops, then recreates, re-applies or moves
        // each preview after its component / transform changed.
        void update(float deltaTime);

        // Plays (or restarts) the entity's preview. Refused in Play mode.
        VFXEditPreviewResult start(EntityHandle entity);
        // Stops the entity's preview, or every preview for an invalid handle; returns how many.
        uint32_t stop(EntityHandle entity);
        // The entity's preview (at most one entry), or every preview for an invalid handle.
        std::vector<VFXEditPreviewStatus> list(EntityHandle entity) const;

    private:
        struct Preview
        {
            VFXInstanceId instanceId = 0;
            std::string assetPath;                      // resolved .vfVFX the instance was created from
            bool loop = true;
            std::vector<vfx::VFXParamOverride> applied; // what the instance carries (apply is cumulative)
            glm::mat4 lastWorld{1.0f};
            float seconds = 0.0f;
            // AssetSaved: 1 = recreate on the update after next, once the runtime update in between
            // has dropped the stale config from the renderer's cache; 0 = recreate now; -1 = none.
            int recreateCountdown = -1;
        };

        // Lock order: previewMutex, then pendingMutex. previewMutex is held across the vfxruntime
        // commands a start/stop/update dispatches; none of them publishes a notification this
        // handler subscribes to.
        mutable std::mutex previewMutex;
        std::unordered_map<EntityHandle, Preview, EntityHandle::Hash> previews;

        mutable std::mutex pendingMutex;
        bool pendingStopAll = false;           // SceneClearedNotification
        std::vector<EntityHandle> pendingStops; // EntityDeletedNotification of a previewed entity

        bool handlersRegistered = false;
        ::events::SubscriptionToken editorModeChangedToken;
        ::events::SubscriptionToken sceneClearedToken;
        ::events::SubscriptionToken entityDeletedToken;
        ::events::SubscriptionToken assetSavedToken;

        // previewMutex held for all of these.
        void applyPendingStopsLocked();
        void destroyAllLocked();
        static VFXEditPreviewStatus statusOf(EntityHandle entity, const Preview& preview);
    };
}
