#pragma once

#include "../../data/VFXSequenceTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/EventDispatcher.hpp"
#include "vfx/VFXSequenceTypes.hpp"
#include "vfx/VFXComboTimeline.hpp"
#include <vfx/VFXScalability.hpp>
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace services
{
    // Owns running VFX combos (VK-1425). A combo is a parent that spawns child
    // VFXInstanceIds from the steps of a .vfVFXSequence asset and forwards
    // play/stop/reset/transform/destroy to them. It is pure orchestration over the
    // existing per-instance vfxruntime command API (dispatched through the
    // EventDispatcher) — no graphics/GPU coupling.
    class VFXSequenceRuntimeServiceImpl
    {
    private:
        struct ActiveStep
        {
            const vfx::VFXSequenceStep* def = nullptr; // points into ComboInstance::data (stable)
            VFXInstanceId childId = 0;
            bool spawned = false;
            bool stopped = false;
            // VK-1498 — this child was Stopped (frozen+hidden) by the whole-combo bounds cull.
            // Records exactly the set to Play back on re-entry so we never revive a child the
            // timeline legitimately stopped (StopAfterDuration) or one that already finished.
            bool frozenByCull = false;
            std::optional<vfx::VFXCuePayload> payload;
        };

        // VK-1453 (Phase 4) — global VFX quality tier snapshot. `valid` is false when no
        // renderer registered GetVFXQualityTierQuery (headless), which disables the tier gate.
        struct CachedTier
        {
            bool valid = false;
            vfx::VFXQualityTier tier = vfx::VFXQualityTier::High;
        };

        // VK-1453 (Phase 4) — renderer cull-state snapshot. A local struct (mirroring the
        // CQRS VFXCullStateResult fields) so this header does NOT pull the services::events
        // VFX events into every includer — that would shadow the global ::events namespace in
        // unrelated services headers. `valid` false => no renderer => never cull.
        struct CachedCull
        {
            bool valid = false;
            glm::mat4 viewProj{1.0f};
            glm::vec3 cameraPos{0.0f};
            bool distanceCullEnabled = false;
            float maxDrawDistance = 0.0f;
        };

        struct ComboInstance
        {
            VFXComboInstanceId id = 0;
            std::shared_ptr<const vfx::VFXSequenceData> data;
            glm::mat4 parentTransform{1.0f};
            bool playing = false;
            bool autoDestroyOnFinish = true;
            // VK-1498 — whole-sequence looping. `loopSequence` (from the component `loop` flag,
            // threaded via the create command) makes the combo rewind + replay its whole
            // schedule at completion instead of idling. `stableLoop` (asset-authored) keeps the
            // VK-1497 variety identical each iteration (rewind); otherwise each loop re-seeds for
            // fresh variety. `loopIteration` drives the deterministic per-iteration seed stream.
            // `comboCulled` is the whole-combo bounds-cull freeze state (children Stopped, tick
            // frozen) — distinct from `paused` so a user pause and a cull can't fight each other.
            bool loopSequence = false;
            bool stableLoop = false;
            uint32_t loopIteration = 0;
            bool comboCulled = false;
            // Entity used to resolve sockets (combo-level attach and per-step sockets).
            EntityHandle socketEntity;
            bool attached = false;       // whole-combo socket attach
            std::string attachSocket;
            bool socketWarned = false;   // warn-once when a socket can't be resolved
            std::vector<ActiveStep> steps;

            // VK-1524 — StepOutput receivers. Event-children are world-anchored, fire-and-forget
            // instances spawned when a bound source step publishes a particle event; they are NOT
            // 1:1 with steps (EveryEvent spawns many per receiver), so they live in their own list
            // rather than in ActiveStep::childId. `firstEventConsumed` (sized to steps) is the
            // FirstEvent latch per receiving step. `retiredSourceGrace` keeps a just-reaped source
            // step's instance id routable for a few frames so its trailing death/collision event
            // (which arrives ~2-3 frames late via GPU readback + the render->update FIFO) still
            // resolves to the source before the combo completes.
            struct EventChild { VFXInstanceId childId = 0; int receivingStepIndex = -1; bool frozenByCull = false; };
            struct RetiredSource { VFXInstanceId childId = 0; int stepIndex = -1; int framesLeft = 0; };
            std::vector<EventChild> eventChildren;
            std::vector<bool> firstEventConsumed;
            std::vector<RetiredSource> retiredSourceGrace;

            // VK-1451 — deterministic transport state. The simulated clock now lives in
            // `timeline` (timeline.elapsed()); `accumulator` carries the sub-fixedStep
            // remainder so the spawn schedule is independent of frame pacing.
            vfx::VFXComboTimeline timeline;
            uint32_t seed = 0;
            float playbackRate = 1.0f;
            float fixedStep = 0.0f;      // 0 => variable step
            float prewarm = 0.0f;
            float accumulator = 0.0f;
            bool paused = false;
            bool prewarmApplied = false;

            // VK-1453 (Phase 4) — cull state + quality tier snapshots captured once per
            // update() tick and read by spawnStep to pre-cull off-screen fire-and-forget
            // steps and to skip effects disabled at the active tier. Defaults (both invalid)
            // => never cull/skip (before the first update, or when headless).
            CachedCull cachedCull;
            CachedTier cachedTier;
        };

        std::unordered_map<VFXComboInstanceId, ComboInstance> combos;
        VFXComboInstanceId nextComboId = 1;
        ::events::SubscriptionToken assetSavedToken;

        // VK-1496 — re-entrancy guard. A ScriptCue step publishes VFXComboCueFiredNotification
        // synchronously (ScriptVFXEventBridge -> mType onComboCue on the update thread). If that
        // script destroys/resets its own combo, doing it immediately would invalidate the
        // `combos` iterator/reference held by update() and triggerCue(). While the guard depth
        // is >0, destroyCombo(id)/resetCombo(id) queue the request and drainPendingComboTeardowns()
        // applies it once the outermost holder releases the reference. Depth-counted (not a bool)
        // so a nested triggerCue() invoked from inside onComboCue does not prematurely drain.
        // Single-threaded (update thread) => a plain int, no atomic.
        int comboTeardownGuard = 0;
        std::vector<std::pair<VFXComboInstanceId, bool>> pendingComboTeardowns; // (id, isReset)

        // VK-1460: AssetSaved is delivered on the publishing (editor) thread; queue the
        // saved paths here and apply them on the update thread (drained at the top of
        // update()) so sequenceCache/childCache are never mutated concurrently with the
        // spawnStep reads. Mirrors VFXRuntimeAdapter's pendingConfigInvalidations pattern.
        std::mutex sequenceInvalidationMutex;
        std::vector<std::string> pendingSequenceInvalidations;

        // VK-1524 — particle step-output events. VFXParticleEventNotification is published on the
        // render thread (GPU readback in VFXSceneRenderer::processEvents); the subscription
        // callback only pushes a mirror of its fields into this FIFO under `particleEventMutex`.
        // update() drains it at the top on the update thread and routes each event to the owning
        // combo's StepOutput receivers. A local mirror struct (like CachedCull) keeps this HEADER
        // free of the services::events VFX headers, which would shadow the global ::events namespace.
        struct PendingParticleEvent
        {
            uint32_t eventType = 0;
            glm::vec3 position{0.0f};
            glm::vec3 velocity{0.0f};
            glm::vec3 color{1.0f};
            float size = 1.0f;
            VFXInstanceId parentInstanceId = 0;
        };
        std::mutex particleEventMutex;
        std::vector<PendingParticleEvent> pendingParticleEvents;
        ::events::SubscriptionToken particleEventToken;

        // Sequence assets are cached as shared_ptr so each combo's step pointers stay valid.
        std::unordered_map<std::string, std::shared_ptr<const vfx::VFXSequenceData>> sequenceCache;

        // VK-1453 (Phase 4) — per child-.vfVFX cull metadata, loaded once per resolved path
        // and invalidated on AssetSaved alongside sequenceCache. Avoids a disk read per spawn
        // when deciding whether an off-screen fire-and-forget step can be culled.
        struct ChildVFXInfo
        {
            math::AABB bounds;            // resolveBounds() of the child effect (local space)
            vfx::VFXScalability scal;      // per-tier scalability profile (applied renderer-side)
            bool cullEligible = false;     // asset opts into pre-spawn distance/frustum cull
            bool valid = false;            // false => load failed; never cull, spawn normally
        };
        std::unordered_map<std::string, ChildVFXInfo> childCache;

        // VK-1453 (Phase 4) — cumulative combo debug counters (GetVFXComboStatsQuery).
        uint32_t culledSpawns = 0;
        // Pool reuse is tracked renderer-side (the renderer owns slot reuse); the service
        // cannot cheaply observe it, so this stays 0 here.
        uint32_t pooledReuses = 0;

        std::shared_ptr<const vfx::VFXSequenceData> loadSequence(const std::string& path);
        const ChildVFXInfo& loadChildInfo(const std::string& path);
        CachedCull queryCullState() const;
        CachedTier queryQualityTier() const;
        void invalidateSequence(const std::string& path);
        void spawnStep(ComboInstance& combo, int stepIndex, const glm::mat4& stepParent,
                       const vfx::VFXCuePayload* payload = nullptr);
        // VK-1524 — drain the marshalled particle-event FIFO (top of update(), forward-only) and
        // route each event to the owning combo's StepOutput receivers. Queries the cull/tier
        // snapshot internally (only when there are events to route) so spawnEventChild reuses the
        // pre-spawn cull/tier gate.
        void drainParticleEvents();
        // VK-1524 — spawn one world-anchored, fire-and-forget event-child for a StepOutput receiver
        // at the event's world position + the receiver's local offset. Returns 0 on cull/failure.
        VFXInstanceId spawnEventChild(ComboInstance& combo, int receivingStepIndex,
                                      const vfx::VFXEventPayload& payload);
        // VK-1524 — destroy every event-child and reset the FirstEvent latches + grace list.
        void clearEventState(ComboInstance& combo);
        // VK-1496 — typed-step fan-out (Sound + ScriptCue). Plain glm/int signatures so the
        // header pulls in NO audio events header (audio commands are dispatched from the .cpp
        // only, avoiding the services::events -> ::events namespace-shadowing pitfall).
        void fireSoundStep(ComboInstance& combo, int stepIndex, const glm::mat4& stepParent);
        void fireScriptCueStep(ComboInstance& combo, int stepIndex);
        void applyComboEvents(ComboInstance& combo, const std::vector<vfx::ComboEvent>& events,
                              const glm::mat4& comboParent, const vfx::VFXCuePayload* manualPayload = nullptr);
        // Rewind + deterministic fixed-step replay of the schedule to `targetSeconds`,
        // then (re)spawn only the steps live at that time. Used by seek and prewarm.
        void replayTo(ComboInstance& combo, float targetSeconds);
        // VK-1498 — on completion of a looping combo, rewind (stableLoop) or re-seed+reset
        // (variety) the schedule and clear per-step state so the next forward update() re-spawns
        // and re-fires cues. No child destroy (none live at completion), no respawn, no publish.
        void restartComboForLoop(ComboInstance& combo);
        // VK-1498 — whole-combo bounds cull: true when a looping combo with authored Fixed bounds
        // is outside the cached frustum/distance. Auto/degenerate bounds or no cull state => false.
        bool computeComboCull(const ComboInstance& combo, const glm::mat4& comboParent) const;
        // VK-1498 — Stop (freeze+hide) or Play back the live, non-stopped children on a cull
        // transition, tracking exactly the frozen set via ActiveStep::frozenByCull.
        void setComboChildrenFrozen(ComboInstance& combo, bool frozen);
        glm::mat4 resolveComboParent(ComboInstance& combo);
        glm::mat4 resolveStepParent(ComboInstance& combo, const ActiveStep& step, const glm::mat4& comboParent);
        glm::mat4 composeStepWorldTransform(const ActiveStep& step, const glm::mat4& stepParent) const;
        void destroyCombo(ComboInstance& combo);
        void publishCueFired(ComboInstance& combo, const std::string& cueName, const vfx::VFXCuePayload& payload);
        void publishNewlyFiredMarkers(ComboInstance& combo, const std::vector<bool>& before);
        // VK-1496 — apply combo destroy/reset requests deferred while suppressComboTeardown was set.
        void drainPendingComboTeardowns();

        static VFXEmitterOverrides toOverrides(const vfx::VFXSequenceStep& step);

    public:
        VFXSequenceRuntimeServiceImpl() = default;
        ~VFXSequenceRuntimeServiceImpl();

        void registerEventHandlers();

        // VK-1451: seed/prewarm/playbackRate/fixedStep are "use asset default" sentinels
        // (seed==0, the floats <0) unless the caller overrides them.
        // VK-1498: loopSequence makes the whole combo rewind + replay its schedule at
        // completion (the component `loop` flag). Orthogonal to autoDestroyOnFinish.
        VFXComboInstanceId createCombo(const std::string& sequenceAssetPath, const glm::mat4& worldTransform,
                                       uint32_t entityId, bool autoDestroyOnFinish,
                                       uint32_t seed = 0, float prewarm = -1.0f,
                                       float playbackRate = -1.0f, float fixedStep = -1.0f,
                                       bool loopSequence = false);
        void destroyCombo(VFXComboInstanceId id);
        void playCombo(VFXComboInstanceId id);
        void stopCombo(VFXComboInstanceId id);
        void resetCombo(VFXComboInstanceId id);
        void setComboTransform(VFXComboInstanceId id, const glm::mat4& worldTransform);
        void attachComboToSocket(VFXComboInstanceId id, EntityHandle entity, const std::string& socketName);
        void detachCombo(VFXComboInstanceId id);
        void triggerCue(VFXComboInstanceId id, const std::string& cueName,
                        const vfx::VFXCuePayload& payload = {});
        bool isComboPlaying(VFXComboInstanceId id) const;
        void update(float deltaTime);

        // VK-1451 — deterministic transport.
        void setComboPaused(VFXComboInstanceId id, bool paused);
        void setComboPlaybackRate(VFXComboInstanceId id, float rate);
        void seekCombo(VFXComboInstanceId id, float seconds);

        // Destroy every combo and all child instances. Used on exit-play teardown.
        void destroyAll();
    };
}
