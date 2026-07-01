#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXSequenceTypes.hpp"
#include <vfx/VFXSequenceTypes.hpp>
#include <glm/glm.hpp>
#include <string>

namespace services::events::vfxsequence
{
    // ============================================================
    // VFX COMBO SEQUENCE RUNTIME COMMANDS (VK-1425)
    //
    // A combo is a parent instance that owns several child VFXInstanceIds spawned
    // from the steps of a .vfVFXSequence asset. These commands forward lifecycle
    // to all children; the combo runtime service translates them into the existing
    // per-instance vfxruntime commands. No graphics/GPU changes are involved.
    // ============================================================

    struct CreateVFXComboInstanceCommand : ::events::ICommand<VFXComboInstanceId>
    {
        std::string sequenceAssetPath;
        glm::mat4 worldTransform{1.0f};
        uint32_t entityId = 0;            // owning entity (0 == none)
        bool autoDestroyOnFinish = true;  // erase the combo once every step has finished
        // VK-1451 timeline controls. Each is a "use asset default" sentinel unless set:
        //   seed         == 0   -> use asset seed (which, if 0, is auto-randomized)
        //   prewarm      <  0   -> use asset prewarm
        //   playbackRate <  0   -> use asset playbackRate
        //   fixedStep    <  0   -> use asset fixedStep
        uint32_t seed = 0;
        float prewarm = -1.0f;
        float playbackRate = -1.0f;
        float fixedStep = -1.0f;
        std::string_view getName() const override { return "CreateVFXComboInstance"; }
    };

    struct DestroyVFXComboInstanceCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "DestroyVFXComboInstance"; }
    };

    struct PlayVFXComboInstanceCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "PlayVFXComboInstance"; }
    };

    struct StopVFXComboInstanceCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "StopVFXComboInstance"; }
    };

    struct ResetVFXComboInstanceCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "ResetVFXComboInstance"; }
    };

    struct SetVFXComboInstanceTransformCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        glm::mat4 worldTransform{1.0f};
        std::string_view getName() const override { return "SetVFXComboInstanceTransform"; }
    };

    // Attach the whole combo to an entity socket. Each frame the combo resolves the
    // socket's current world transform itself and composes each step's local offset
    // on top of it (it does NOT use AttachVFXInstanceToSocket for children, which
    // would overwrite the per-step offset).
    struct AttachVFXComboInstanceToSocketCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        uint64_t entityHandle = 0;
        std::string socketName;
        std::string_view getName() const override { return "AttachVFXComboInstanceToSocket"; }
    };

    struct DetachVFXComboInstanceCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "DetachVFXComboInstance"; }
    };

    // Fire all not-yet-spawned cue-driven steps whose cueName matches.
    struct TriggerVFXComboCueCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        std::string cueName;
        vfx::VFXCuePayload payload;
        std::string_view getName() const override { return "TriggerVFXComboCue"; }
    };

    struct UpdateVFXSequenceRuntimeCommand : ::events::ICommand<void>
    {
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdateVFXSequenceRuntime"; }
    };

    // ============================================================
    // VK-1451 — deterministic transport controls
    // ============================================================

    struct SetVFXComboPausedCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        bool paused = true;
        std::string_view getName() const override { return "SetVFXComboPaused"; }
    };

    struct SetVFXComboPlaybackRateCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        float rate = 1.0f;
        std::string_view getName() const override { return "SetVFXComboPlaybackRate"; }
    };

    // Deterministically jump the combo's schedule to `seconds`: rewind + fixed-step
    // replay. Freshly spawned runtime children warm from t=0 (GPU particle buffers
    // cannot be rewound) — the spawn schedule is what is reproducible.
    struct SeekVFXComboCommand : ::events::ICommand<void>
    {
        VFXComboInstanceId comboId = 0;
        float seconds = 0.0f;
        std::string_view getName() const override { return "SeekVFXCombo"; }
    };

    struct IsVFXComboInstancePlayingQuery : ::events::IQuery<bool>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "IsVFXComboInstancePlaying"; }
    };

    // ============================================================
    // VK-1453 (Phase 4) — combo debug stats for the VFX debug window
    // ============================================================

    struct VFXComboStatsResult
    {
        uint32_t activeCombos = 0;        // combos currently tracked by the service
        uint32_t playingCombos = 0;       // combos with steps still to spawn / children live
        uint32_t liveChildInstances = 0;  // child VFX instances currently owned by combos
        uint32_t culledSpawns = 0;        // steps skipped by pre-spawn cull (cumulative)
        uint32_t pooledReuses = 0;        // child spawns satisfied from the instance pool (cumulative)
    };

    struct GetVFXComboStatsQuery : ::events::IQuery<VFXComboStatsResult>
    {
        std::string_view getName() const override { return "GetVFXComboStats"; }
    };
}
