#pragma once

// VK-1435 — UI Layer Builder preview provider interface.
//
// Abstracts the offscreen WYSIWYG UI-canvas preview (Layer B UILayerPreviewController) for
// the editor, keyed by PreviewInstanceId so each open builder window owns an independent
// controller + offscreen target. The Core-side UILayerPreviewAdapter implements this by
// owning an unordered_map<PreviewInstanceId, unique_ptr<UILayerPreviewController>>.
//
// Unlike the Prefab Rig preview (VK-1433), this carries NO subtree descriptor across the
// boundary: the authored UI entities already live in the one singleton registry (the
// builder drives them through the existing UIComponentService CQRS), so the controller is
// handed only the live canvas-root EntityHandle + a reference resolution and reads the
// subtree directly. Element edits never come through here — they go straight to
// UIComponentService. This provider owns only render-specific concerns: the offscreen
// render, and hit-test / resolved-rect computed against the preview's reference extent
// (the existing getUIResolvedRectPixels resolves against the play viewport, which is the
// wrong space for the builder canvas).
//
// Everything below uses only Editor-safe types (EntityHandle, glm, the UI DTOs).

#include <glm/glm.hpp>
#include <optional>
#include "../PreviewInstanceId.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp" // UIResolvedRectData

namespace services
{
    class IUILayerPreviewProvider
    {
    public:
        virtual ~IUILayerPreviewProvider() = default;

        // Lifecycle. initUILayerPreview creates + inits the controller for this instance;
        // buildUILayerPreview binds the live canvas-root entity and (re)creates the offscreen
        // target at the reference resolution; cleanUpUILayerPreview tears the instance down.
        virtual void initUILayerPreview(PreviewInstanceId instanceId) = 0;
        virtual bool buildUILayerPreview(PreviewInstanceId instanceId, EntityHandle canvasRoot,
                                         uint32_t refWidth, uint32_t refHeight) = 0;
        virtual void cleanUpUILayerPreview(PreviewInstanceId instanceId) = 0;
        virtual bool isUILayerPreviewBuilt(PreviewInstanceId instanceId) const = 0;

        // Change the WYSIWYG reference resolution (recreates the offscreen target).
        virtual void setUILayerReferenceResolution(PreviewInstanceId instanceId,
                                                   uint32_t refWidth, uint32_t refHeight) = 0;

        // Renders the bound canvas subtree to its offscreen image; returns the ImGui
        // descriptor set for ImGui::Image() (or nullptr if not built).
        virtual void* renderUILayerPreview(PreviewInstanceId instanceId) = 0;

        // Hit-test: top-most UI element whose resolved rect contains refPx (reference-pixel
        // space, top-left origin, y down). Returns EntityHandle::invalid() on a miss.
        virtual EntityHandle pickUILayerElementAt(PreviewInstanceId instanceId,
                                                  glm::vec2 refPx) const = 0;

        // Resolved on-screen rect of a UI element in the preview's reference-pixel space
        // (the space the builder's on-canvas handles draw in). nullopt if not resolvable.
        virtual std::optional<UIResolvedRectData> getUILayerResolvedRect(PreviewInstanceId instanceId,
                                                                         EntityHandle entity) const = 0;
    };
}
