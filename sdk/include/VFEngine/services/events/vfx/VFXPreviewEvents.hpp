#pragma once

#include "../EventTypes.hpp"
#include "../../providers/vfx/IVFXPreviewProvider.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <string>

namespace services::events::vfxpreview
{
    // ============================================================
    // VFX PREVIEW COMMANDS (Multi-instance support via instanceId)
    // ============================================================

    struct InitVFXPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitVFXPreview"; }
    };

    struct CleanUpVFXPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpVFXPreview"; }
    };

    struct SetVFXParamsCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        VFXPreviewParams params;
        std::string_view getName() const override { return "SetVFXParams"; }
    };

    struct UpdateVFXCameraCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        float time = 0.0f;
        std::string_view getName() const override { return "UpdateVFXCamera"; }
    };

    struct UpdateVFXSimulationCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        float deltaTime = 0.0f;
        std::string_view getName() const override { return "UpdateVFXSimulation"; }
    };

    struct PlayVFXCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PlayVFX"; }
    };

    struct PauseVFXCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PauseVFX"; }
    };

    struct StopVFXCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "StopVFX"; }
    };

    // ============================================================
    // VFX PREVIEW QUERIES
    // ============================================================

    struct RenderVFXPreviewQuery : ::events::IQuery<ViewportTextureHandle>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderVFXPreview"; }
    };
}
