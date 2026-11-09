#pragma once

#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/VFXTypes.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace services
{
    // VK-1657 — one edit-mode preview of an entity's VFX component (VFXEditPreviewHandler).
    struct VFXEditPreviewStatus
    {
        EntityHandle entity;
        VFXInstanceId instanceId = 0;
        std::string assetPath; // resolved .vfVFX path
        bool loop = true;
        float seconds = 0.0f;  // edit-mode time since the preview (re)started
    };

    struct VFXEditPreviewResult
    {
        bool started = false;
        std::string error;
        VFXEditPreviewStatus status;
    };
}

// Namespace vfxeditpreview: services::events::vfxpreview belongs to the VFX Editor window preview.
namespace services::events::vfxeditpreview
{
    // Plays the entity's VFX component, with its per-entity overrides, in the editor viewport while
    // in Edit mode; restarts a running preview. The preview follows the entity's transform and
    // component edits, and ends on Stop, Edit->Play, scene clear, entity deletion or component
    // removal. Refused in Play mode. Main thread (MCP drain / ImGui).
    struct StartVFXEditPreviewCommand : ::events::ICommand<VFXEditPreviewResult>
    {
        EntityHandle entity;
        std::string_view getName() const override { return "StartVFXEditPreview"; }
    };

    // Stops the entity's preview; an invalid handle stops every preview. Returns how many stopped.
    struct StopVFXEditPreviewCommand : ::events::ICommand<uint32_t>
    {
        EntityHandle entity;
        std::string_view getName() const override { return "StopVFXEditPreview"; }
    };

    // The entity's preview (at most one entry); an invalid handle lists every preview.
    struct GetVFXEditPreviewsQuery : ::events::IQuery<std::vector<VFXEditPreviewStatus>>
    {
        EntityHandle entity;
        std::string_view getName() const override { return "GetVFXEditPreviews"; }
    };
}
