#pragma once

#include "EventTypes.hpp"
#include "../data/VFXTypes.hpp"
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

    struct DestroyAllVFXInstancesCommand : ::events::ICommand<void>
    {
        std::string_view getName() const override { return "DestroyAllVFXInstances"; }
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

    struct SetVFXRuntimeCameraCommand : ::events::ICommand<void>
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 cameraPos{0.0f};
        float time = 0.0f;
        std::string_view getName() const override { return "SetVFXRuntimeCamera"; }
    };

    // ============================================================
    // VFX RUNTIME QUERIES
    // ============================================================

    struct IsVFXInstancePlayingQuery : ::events::IQuery<bool>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "IsVFXInstancePlaying"; }
    };

    struct IsVFXInstanceActiveQuery : ::events::IQuery<bool>
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "IsVFXInstanceActive"; }
    };

    struct GetVFXInstanceCountQuery : ::events::IQuery<size_t>
    {
        std::string_view getName() const override { return "GetVFXInstanceCount"; }
    };

    struct GetTotalVFXParticleCountQuery : ::events::IQuery<size_t>
    {
        std::string_view getName() const override { return "GetTotalVFXParticleCount"; }
    };

    struct IsVFXRuntimeInitializedQuery : ::events::IQuery<bool>
    {
        std::string_view getName() const override { return "IsVFXRuntimeInitialized"; }
    };

    // ============================================================
    // VFX RUNTIME NOTIFICATIONS
    // ============================================================

    struct VFXInstanceCompletedNotification : ::events::INotification
    {
        VFXInstanceId instanceId = 0;
        std::string_view getName() const override { return "VFXInstanceCompleted"; }
    };
}
