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

    struct VFXSequenceData
    {
        std::string version = "1.0";
        std::string uuid;
        std::string name = "Unnamed Sequence";
        std::vector<VFXSequenceStep> steps;
    };
}
