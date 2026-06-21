#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXSequenceTypes.hpp"
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
        std::string_view getName() const override { return "TriggerVFXComboCue"; }
    };

    struct UpdateVFXSequenceRuntimeCommand : ::events::ICommand<void>
    {
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdateVFXSequenceRuntime"; }
    };

    struct IsVFXComboInstancePlayingQuery : ::events::IQuery<bool>
    {
        VFXComboInstanceId comboId = 0;
        std::string_view getName() const override { return "IsVFXComboInstancePlaying"; }
    };
}
