#pragma once

#include "../EventTypes.hpp"
#include "../../data/VFXTypes.hpp"
#include <vfx/VFXScalability.hpp>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

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

    struct CreateVFXChannelCommand : ::events::ICommand<VFXInstanceId>
    {
        std::string vfxAssetPath;
        // 0 derives the count from the compatible authored burst configuration.
        uint32_t particlesPerRequest = 0;
        std::string_view getName() const override { return "CreateVFXChannel"; }
    };

    struct EmitToVFXChannelCommand : ::events::ICommand<void>
    {
        VFXInstanceId channelId = 0;
        VFXChannelEmitParams params;
        std::string_view getName() const override { return "EmitToVFXChannel"; }
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

    // Attach a live instance to an entity socket. Each frame, before instance
    // transforms update, the instance's worldTransform is set from the socket's
    // current world transform. The attachment auto-clears when the entity is
    // invalid, the socket no longer exists, or the instance is destroyed.
    struct AttachVFXInstanceToSocketCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        uint64_t entityHandle = 0;
        std::string socketName;
        std::string_view getName() const override { return "AttachVFXInstanceToSocket"; }
    };

    struct DetachVFXInstanceCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "DetachVFXInstance"; }
    };

    struct ApplyVFXInstanceOverridesCommand : ::events::ICommand<void>
    {
        VFXInstanceId instanceId = 0;
        VFXEmitterOverrides overrides;
        std::string_view getName() const override { return "ApplyVFXInstanceOverrides"; }
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
        // VK-1453 (Phase 4)
        uint32_t culledEmitters = 0;
        uint32_t throttledEmitters = 0;
        float vfxCullDistance = 0.0f;
        uint32_t eventsThisFrame = 0;
        uint32_t rawEventsThisFrame = 0;
        uint32_t eventBudget = 0;
        bool eventsDropped = false;
        uint32_t channelListeners = 0;
        uint32_t channelRawRequests = 0;
        uint32_t channelAcceptedRequests = 0;
        uint32_t channelRingDroppedRequests = 0;
        uint32_t channelParticleDroppedRequests = 0;
        uint32_t channelRequestBudget = 0;
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

    // ============================================================
    // VK-1453 (Phase 4) — pre-cull, scalability tier, debug snapshots
    // ============================================================

    // Camera + cull configuration snapshot, sourced from the renderer, used by the
    // combo service to pre-cull off-screen fire-and-forget effects before spawning.
    struct VFXCullStateResult
    {
        bool valid = false;
        glm::mat4 viewProj{1.0f};
        glm::vec3 cameraPos{0.0f};
        bool distanceCullEnabled = false;
        float maxDrawDistance = 0.0f;
    };

    struct GetVFXCullStateQuery : ::events::IQuery<VFXCullStateResult>
    {
        std::string_view getName() const override { return "GetVFXCullState"; }
    };

    // Global VFX quality tier (drives per-asset scalability profiles).
    struct SetVFXQualityTierCommand : ::events::ICommand<void>
    {
        vfx::VFXQualityTier tier = vfx::VFXQualityTier::High;
        std::string_view getName() const override { return "SetVFXQualityTier"; }
    };

    struct GetVFXQualityTierQuery : ::events::IQuery<vfx::VFXQualityTier>
    {
        std::string_view getName() const override { return "GetVFXQualityTier"; }
    };

    // Per-instance debug snapshot for the VFX debug window (bounds/cull table).
    struct VFXInstanceDebugEntry
    {
        uint32_t id = 0;
        glm::vec3 worldPosition{0.0f};
        glm::vec3 extents{0.0f};
        bool inFrustum = true;
        uint8_t lod = 0;
        uint32_t particleCount = 0;
        uint8_t priority = 2;
    };

    struct VFXInstanceDebugResult
    {
        std::vector<VFXInstanceDebugEntry> instances;
    };

    struct GetVFXInstanceDebugQuery : ::events::IQuery<VFXInstanceDebugResult>
    {
        std::string_view getName() const override { return "GetVFXInstanceDebug"; }
    };

    // Recent deduplicated VFX runtime warnings (AC6) for the debug window.
    struct VFXWarningInfo
    {
        std::string source;
        std::string message;
        uint32_t count = 0;
        uint64_t lastSeq = 0;
    };

    struct VFXRecentWarningsResult
    {
        std::vector<VFXWarningInfo> warnings;
    };

    struct GetVFXRecentWarningsQuery : ::events::IQuery<VFXRecentWarningsResult>
    {
        std::string_view getName() const override { return "GetVFXRecentWarnings"; }
    };

}
