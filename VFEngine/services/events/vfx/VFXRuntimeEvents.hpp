#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include <glm/glm.hpp>
#include <string>

namespace services::events::vfxruntime
{
    // ============================================================
    // VFX RUNTIME COMMANDS
    // ============================================================

    struct CreateVFXInstanceCommand : ::events::ICommand<VFXInstanceId>
    {
        VFXRuntimeParams params;
        std::string_view getName() const override { return "CreateVFXInstance"; }
    };

    struct DestroyVFXInstanceCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "DestroyVFXInstance"; }
    };

    struct SetVFXInstanceTransformCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        glm::mat4 worldTransform{1.0f};
        std::string_view getName() const override { return "SetVFXInstanceTransform"; }
    };

    struct PlayVFXInstanceCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "PlayVFXInstance"; }
    };

    struct StopVFXInstanceCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "StopVFXInstance"; }
    };

    struct ResetVFXInstanceCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "ResetVFXInstance"; }
    };

    struct UpdateVFXRuntimeCommand : ::events::ICommand<void>
    {
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdateVFXRuntime"; }
    };

    struct IsVFXInstancePlayingQuery : ::events::IQuery<bool>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "IsVFXInstancePlaying"; }
    };
}
