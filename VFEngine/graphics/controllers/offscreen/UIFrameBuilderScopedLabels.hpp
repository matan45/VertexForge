#pragma once
#include "../../render/ui/UITextRenderTypes.hpp"
#include <entt/entt.hpp>
#include <vector>

// VK-1435 — cross-TU entry for the UI Layer Builder's scoped label emit. The four screen-space
// label emitters (UILabel / text-input / dropdown options / window titles) are file-local to
// UIFrameBuilder.cpp; this single free function runs them scoped to one sandbox canvas so the
// scoped image path (UIFrameBuilderScreenSpace.cpp) can collect its labels without forking the
// emit logic. Mirrors the ui_screenspace cross-TU helper pattern (UIScreenSpaceScroll.hpp).
namespace controllers::offscreen
{
    // Emits the screen-space text draw data for the subtree rooted at scopedCanvas, resolved at
    // (viewportW, viewportH), appending into `out`. Only labels whose owning canvas == scopedCanvas
    // are emitted, and the scoped active check treats scopedCanvas as active (sandbox root may be
    // intentionally inactive). Never touches RenderPassHandler.
    void emitScopedCanvasLabels(
        entt::registry& registry,
        std::vector<render::ui::UITextRenderData>& out,
        float viewportW, float viewportH,
        entt::entity scopedCanvas);
}
