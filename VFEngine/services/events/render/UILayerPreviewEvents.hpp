#pragma once

// VK-1435 — CQRS events for the UI Layer Builder preview (Layer C boundary).
// Per-instance commands keyed by PreviewInstanceId; render via a query returning
// ViewportTextureHandle. Mirrors the MeshPreview / PrefabRigPreview event shape.
//
// Element edits are NOT here: the builder authors live entities through the existing
// UIComponentService CQRS (SetUIRectDataCommand, AddUI*ComponentCommand, theme commands,
// ...). These events cover only the offscreen preview + reference-extent hit-test/rect.

#include "../EventTypes.hpp"
#include "../../providers/render/IUILayerPreviewProvider.hpp"
#include "../../providers/PreviewInstanceId.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp" // ViewportTextureHandle, UIResolvedRectData
#include <glm/glm.hpp>
#include <optional>

namespace services::events::uilayerpreview
{
    struct InitUILayerPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitUILayerPreview"; }
    };

    struct BuildUILayerPreviewCommand : ::events::ICommand<bool>
    {
        PreviewInstanceId instanceId;
        EntityHandle canvasRoot;
        uint32_t refWidth = 1920;
        uint32_t refHeight = 1080;
        std::string_view getName() const override { return "BuildUILayerPreview"; }
    };

    struct CleanUpUILayerPreviewCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpUILayerPreview"; }
    };

    struct SetUILayerReferenceResolutionCommand : ::events::ICommand<void>
    {
        PreviewInstanceId instanceId;
        uint32_t refWidth = 1920;
        uint32_t refHeight = 1080;
        std::string_view getName() const override { return "SetUILayerReferenceResolution"; }
    };

    // ---- Queries ----

    struct RenderUILayerPreviewQuery : ::events::IQuery<ViewportTextureHandle>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderUILayerPreview"; }
    };

    struct IsUILayerPreviewBuiltQuery : ::events::IQuery<bool>
    {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsUILayerPreviewBuilt"; }
    };

    // Top-most element at refPx (reference-pixel space, top-left origin, y down);
    // returns EntityHandle::invalid() on a miss.
    struct PickUILayerElementAtQuery : ::events::IQuery<EntityHandle>
    {
        PreviewInstanceId instanceId;
        glm::vec2 refPx{0.0f, 0.0f};
        std::string_view getName() const override { return "PickUILayerElementAt"; }
    };

    // Resolved rect of an element in the preview's reference-pixel space (for handles).
    struct GetUILayerResolvedRectQuery : ::events::IQuery<std::optional<UIResolvedRectData>>
    {
        PreviewInstanceId instanceId;
        EntityHandle entity;
        std::string_view getName() const override { return "GetUILayerResolvedRect"; }
    };
}
