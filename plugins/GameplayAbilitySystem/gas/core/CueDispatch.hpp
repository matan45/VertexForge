#pragma once

// Gameplay Ability System (VK-816) — cue dispatch (ENGINE-FREE, testable).
//
// Resolves gameplay-cue ids to GameplayCueSpecs and emits one CueEvent per cue
// to a caller-supplied sink. The PLUGIN wires the sink to the engine VFX/audio
// APIs; unit tests wire it to a recording mock — so the resolve/emit logic is
// covered without an engine.

#include "GASAssets.hpp"

#include <functional>
#include <string>
#include <vector>

namespace gas
{
    // A resolved cue ready to be realised by the engine (VFX + audio at a world
    // position). Pure data; no engine types.
    struct CueEvent
    {
        std::string cueId;
        CueTrigger trigger = CueTrigger::OnApply;
        std::string vfxPath;
        std::string audioPath;
        std::string attachSocket;
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    using CueResolver = std::function<const GameplayCueSpec*(const std::string&)>;
    using CueSink = std::function<void(const CueEvent&)>;

    // Resolve each cue id and emit a CueEvent to the sink at world (x,y,z).
    // Unknown/empty ids are skipped; the dispatcher never throws. Returns the
    // number of cues actually dispatched.
    inline int dispatchCues(const std::vector<std::string>& cueIds,
                            const CueResolver& resolve, const CueSink& sink,
                            float x, float y, float z)
    {
        int count = 0;
        for (const auto& id : cueIds)
        {
            if (id.empty() || !resolve) continue;
            const GameplayCueSpec* spec = resolve(id);
            if (!spec) continue;
            CueEvent ev;
            ev.cueId = spec->id;
            ev.trigger = spec->trigger;
            ev.vfxPath = spec->vfxPath;
            ev.audioPath = spec->audioPath;
            ev.attachSocket = spec->attachSocket;
            ev.x = x;
            ev.y = y;
            ev.z = z;
            if (sink) sink(ev);
            ++count;
        }
        return count;
    }
}
