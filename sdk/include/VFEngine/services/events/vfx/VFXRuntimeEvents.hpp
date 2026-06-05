#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include <glm/glm.hpp>
#include <optional>
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

    struct VFXBudgetStatsResult
    {
        uint32_t activeEmitters = 0;
        uint32_t maxEmitters = 0;
        uint32_t allocatedParticles = 0;
        uint32_t maxParticles = 0;
        uint32_t lodCounts[4] = {0, 0, 0, 0};
        float fragmentationPercent = 0.0f;
        uint32_t poolWarmSlots = 0;
        uint32_t poolUsedSlots = 0;
        uint32_t poolTotalSlots = 0;
    };

    struct GetVFXBudgetStatsQuery : ::events::IQuery<VFXBudgetStatsResult>
    {
        std::string_view getName() const override { return "GetVFXBudgetStats"; }
    };

    struct VFXLODConfigResult
    {
        float lod0Distance = 50.0f;
        float lod1Distance = 100.0f;
        float lod2Distance = 200.0f;
        float transitionZone = 10.0f;
    };

    struct GetVFXLODConfigQuery : ::events::IQuery<VFXLODConfigResult>
    {
        std::string_view getName() const override { return "GetVFXLODConfig"; }
    };

    struct SetVFXLODConfigCommand : ::events::ICommand<void>
    {
        float lod0Distance = 50.0f;
        float lod1Distance = 100.0f;
        float lod2Distance = 200.0f;
        float transitionZone = 10.0f;
        std::string_view getName() const override { return "SetVFXLODConfig"; }
    };

}
