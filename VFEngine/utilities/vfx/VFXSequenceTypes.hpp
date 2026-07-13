#pragma once

#include "../asset/AssetRef.hpp"
#include "VFXEventTypes.hpp"
#include "VFXParameterRegistry.hpp"
#include "VFXTypes.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace vfx
{
    // How a sequence step stops once it has been triggered.
    //   PlayToCompletion  - let the child VFX finish on its own (or run until its
    //                       own loop is turned off).
    //   StopAfterDuration - force-stop the child after `duration` seconds.
    enum class VFXStepStopMode : uint8_t
    {
        PlayToCompletion = 0,
        StopAfterDuration = 1
    };

    // VK-1496 — what a sequence step does when its timeline slot fires. Additive tag:
    // absent in v1.3 files => VFX (the only pre-v1.4 behavior), so old assets load
    // unchanged. Decal/Light/CameraShake are intentionally NOT implemented this pass
    // (camera shake stays a cue + mType script); the enum leaves room for them.
    enum class VFXStepKind : uint8_t
    {
        VFX = 0,        // spawn a child .vfVFX (default; pre-v1.4 behavior)
        Sound = 1,      // fire-and-forget a .vfAudio one-shot via the audio service
        ScriptCue = 2   // publish a named cue to gameplay scripts (onComboCue)
    };

    // VK-1524 — what schedules a step. Additive tag: absent in <=v1.5 files, where the
    // codec derives it from `cueName` (empty => Time, non-empty => Cue) so old assets load
    // unchanged.
    //   Time       - fires when the combo clock reaches `startTime` (default; pre-v1.6 time-driven).
    //   Cue        - fires when a matching named cue is triggered (pre-v1.6 cue-driven).
    //   StepOutput - fires when a bound source step publishes a named particle event; the step
    //                spawns at that event's WORLD-space location. Never scheduled by the timeline.
    enum class VFXStepTrigger : uint8_t
    {
        Time = 0,
        Cue = 1,
        StepOutput = 2
    };

    // VK-1524 — how a StepOutput receiver consumes matching source events.
    //   FirstEvent - spawn exactly once, from the first matching event.
    //   EveryEvent - spawn once per matching event, bounded by the receiver's `eventBudget`.
    enum class VFXEventConsumption : uint8_t
    {
        FirstEvent = 0,
        EveryEvent = 1
    };

    // Optional gameplay payload carried by an event marker, a ScriptCue step, or (VK-1524)
    // a particle step-output event. Declared before VFXSequenceStep because a ScriptCue step
    // embeds one by value. This is the single spawn payload shared by every producer — cue
    // markers, CPU particle events, and (future) VK-1501 GPU event->child requests — so the
    // receiving seam (spawnStep with payload.position) never needs another schema redesign.
    struct VFXCuePayload
    {
        std::optional<glm::vec3> position;
        std::optional<glm::vec3> velocity;   // VK-1524 — impact velocity (particle events)
        std::optional<glm::vec3> normal;     // VK-1524 — reserved: no GPU normal producer yet
        std::optional<glm::vec4> color;
        std::optional<float> scalar;
        std::vector<VFXParamOverride> custom;
    };

    // VK-1524 — clarity alias for the particle-event / GPU-event spawn payload. It is the same
    // type as VFXCuePayload (one consumer: spawnStep); the alias documents intent at call sites.
    using VFXEventPayload = VFXCuePayload;

    // One entry in a combo/sequence, placed in time (or behind a named cue) with an
    // optional local transform and socket attachment. `kind` selects the payload:
    //   VFX       - spawn `vfxRef` with name-keyed `overrides` applied.
    //   Sound     - play `audioRef` (2D, or 3D at the step world transform when
    //               `spatialized`); fire-and-forget, no retained handle.
    //   ScriptCue - publish `emitCueName` + `cuePayload` to gameplay scripts.
    struct VFXSequenceStep
    {
        VFXStepKind kind = VFXStepKind::VFX; // VK-1496 — payload selector (default = VFX)

        asset::AssetRef vfxRef;             // [VFX] GUID ref to an existing .vfVFX
        std::string label;                  // editor display name
        float startTime = 0.0f;             // seconds from combo play()
        std::string cueName;                // "" => time-driven; else fired by named cue
        glm::vec3 localPosition{0.0f};
        glm::vec3 localEulerDeg{0.0f};
        glm::vec3 localScale{1.0f};
        bool loop = false;
        float duration = 0.0f;              // 0 => play to child completion
        VFXStepStopMode stopMode = VFXStepStopMode::PlayToCompletion;
        std::string socketName;             // optional per-step socket
        std::vector<VFXParamOverride> overrides;

        // VK-1496 [Sound] — fire-and-forget audio one-shot.
        asset::AssetRef audioRef;           // .vfAudio to play
        float volume = 1.0f;
        float pitch = 1.0f;
        bool spatialized = false;           // false => 2D; true => 3D at step world transform

        // VK-1496 [ScriptCue] — cue published at fire time. `emitCueName` is the cue to
        // PUBLISH and is deliberately separate from `cueName` (which means "fired BY a
        // cue" and drives the timeline's cue-driven detection): a ScriptCue step is
        // time-driven (empty `cueName`) so the timeline fires it at `startTime`.
        std::string emitCueName;
        VFXCuePayload cuePayload;

        // VK-1497 — deterministic per-step variety, resolved once at VFXComboTimeline
        // reset()/rewind() from the combo seed so seek/prewarm/replay reproduce the exact
        // same choices. Applies to every step kind.
        float probability = 1.0f;   // ungrouped: independent play chance in [0,1] (>=1 always plays)
        int32_t variantGroup = -1;  // >=0: exactly one member of the group plays (uniform in v1);
                                    //      probability is reserved as a future selection weight

        // VK-1524 — scheduling trigger. Pre-v1.6 assets have no `trigger` field; the codec
        // derives it from `cueName` (empty => Time, non-empty => Cue) so behavior is unchanged.
        VFXStepTrigger trigger = VFXStepTrigger::Time;

        // VK-1524 [source] — this step publishes a named spatial output when its live child
        // .vfVFX emits `outputEventType` (death/collision/...). Empty name => not a source.
        // A step must not be both a source and a StepOutput receiver (validation Error).
        std::string outputEventName;
        vfx::VFXEventType outputEventType = vfx::VFXEventType::OnDeath;

        // VK-1524 [receiver] — when `trigger == StepOutput`, bind to source step `sourceStepIndex`
        // and match its output `sourceEventName`; spawn at the event's world position offset by
        // this step's local transform. Socket is ignored for StepOutput steps (world-anchored).
        int32_t sourceStepIndex = -1;
        std::string sourceEventName;
        VFXEventConsumption eventConsumption = VFXEventConsumption::FirstEvent;
        uint32_t eventBudget = 16;  // EveryEvent: max LIVE event-children spawned by this receiver
        // Which impact fields the receiver inherits from the event payload (position is always
        // applied — it is the spawn anchor). Opt-in, mirroring the emitter sub-emitter inherit flags.
        bool inheritVelocity = false;
        bool inheritColor = false;
        bool inheritScalar = false;
        bool inheritNormal = false;  // reserved: no GPU normal producer yet (inert this slice)
    };

    // A one-shot timeline event marker (VK-1451). When the combo clock crosses
    // `time` it fires the engine's named-cue mechanism: every not-yet-spawned
    // cue-driven step whose `cueName` matches is spawned. Markers are time-based
    // (no RNG) so they replay deterministically across seek/prewarm.
    struct VFXSequenceEventMarker
    {
        float time = 0.0f;
        std::string cueName;
        VFXCuePayload payload;
    };

    struct VFXSequenceData
    {
        std::string version = "1.0";
        std::string uuid;
        std::string name = "Unnamed Sequence";
        std::vector<VFXSequenceStep> steps;

        // VK-1451 — deterministic timeline controls. All additive with safe
        // defaults so existing .vfVFXSequence assets load unchanged.
        uint32_t seed = 0;          // 0 => auto-random per combo at runtime
        float    playbackRate = 1.0f;
        float    fixedStep = 0.0f;  // 0 => variable step (Phase-1 behavior)
        float    prewarm = 0.0f;    // seconds to fast-forward when the combo starts
        std::vector<VFXSequenceEventMarker> eventMarkers;

        // VK-1453 (Phase 4) — aggregate bounds over the sequence's steps. Additive
        // with a neutral default (Auto) so existing .vfVFXSequence files are unaffected.
        VFXBounds bounds;

        // VK-1498 — when this sequence is looped, keep the VK-1497 variety (probability /
        // variantGroup rolls) IDENTICAL every iteration instead of re-rolling for variety.
        // Default false = re-roll each loop (the ticket's default). Additive/back-compat.
        bool stableLoop = false;
    };
}
