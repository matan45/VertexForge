#pragma once

#include "../asset/AssetRef.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <utility>
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

    // One entry in a combo/sequence: a child .vfVFX placed in time (or behind a
    // named cue), with an optional local transform, socket attachment, and
    // name-keyed parameter overrides applied when the step is spawned.
    struct VFXSequenceStep
    {
        asset::AssetRef vfxRef;             // GUID ref to an existing .vfVFX
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
        std::vector<std::pair<std::string, float>>     scalarOverrides;
        std::vector<std::pair<std::string, glm::vec4>> vectorOverrides;
    };

    // A one-shot timeline event marker (VK-1451). When the combo clock crosses
    // `time` it fires the engine's named-cue mechanism: every not-yet-spawned
    // cue-driven step whose `cueName` matches is spawned. Markers are time-based
    // (no RNG) so they replay deterministically across seek/prewarm.
    struct VFXSequenceEventMarker
    {
        float time = 0.0f;
        std::string cueName;
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
    };
}
