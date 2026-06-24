#pragma once

// VK-1435 — UI Layer Builder on-canvas 2D edit math (pure, CPU-only, testable).
//
// All geometry is in REFERENCE-PIXEL space: top-left origin, y down — the same space
// the offscreen preview renders in and that IUILayerPreviewProvider::getUILayerResolvedRect
// reports. The builder's handles draw in this space, then map to/from screen space via the
// letterbox helpers below (the offscreen image is rendered at the reference resolution and
// scaled into the ImGui window).
//
// Editing rule (from the plan): the handles edit a UIRectData's anchoredPosition / sizeDelta
// (and, for re-anchor, anchorMin/Max) by INVERTING resolvePixelRect — never SetUIRectPixels
// (that is lossy: it zeroes sizeDelta + anchoredPosition). resolvePixelRect lives in
// utilities/ui/UIRectMath.hpp; this header mirrors only the inverse so the editor stays free
// of utilities/scene/entt. The forward math kept here for the inverse to be self-consistent
// is byte-identical to UIRectMath::resolvePixelRect with parentW=refW, parentH=refH, scale=1.

#include "data/DTOs.hpp" // services::UIRectData, services::UIResolvedRectData
#include <glm/glm.hpp>
#include <array>
#include <optional>

namespace windows::uilayer
{
    // A resolved on-canvas rect in reference-pixel space (top-left origin, y down).
    struct RefRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        float right() const { return x + w; }
        float bottom() const { return y + h; }
        bool contains(glm::vec2 p) const
        {
            return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
        }
    };

    inline RefRect toRefRect(const services::UIResolvedRectData& r)
    {
        return RefRect{r.x, r.y, r.w, r.h};
    }

    // The eight resize handles plus the move body. Order matters for hit-testing
    // (corners take priority over edges, edges over body — see hitTestHandle).
    enum class HandleKind
    {
        None,
        Body,        // drag interior → move
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    // ---- Letterbox mapping (reference px <-> ImGui screen px) -------------------
    //
    // The offscreen image is rendered at refExtent and drawn into an ImGui content
    // region of arbitrary size; we letterbox (preserve aspect, center) so handles
    // line up pixel-exactly with the rendered widgets. originScreen is the top-left
    // of the *content region* (ImGui::GetCursorScreenPos before the Image call).

    struct LetterboxMapping
    {
        glm::vec2 originScreen{0.0f, 0.0f}; // screen pos of the letterboxed image's top-left
        glm::vec2 imageSizeScreen{0.0f, 0.0f}; // on-screen size of the letterboxed image
        glm::vec2 refExtent{1920.0f, 1080.0f}; // reference resolution
        float scale = 1.0f;                    // imageSizeScreen / refExtent (uniform)

        glm::vec2 refToScreen(glm::vec2 ref) const
        {
            return originScreen + ref * scale;
        }
        glm::vec2 screenToRef(glm::vec2 screen) const
        {
            if (scale <= 0.0f) return glm::vec2(0.0f);
            return (screen - originScreen) / scale;
        }
    };

    // Build a letterbox mapping that fits refExtent inside the given content region
    // (regionOrigin/regionSize in screen px), preserving aspect and centering.
    LetterboxMapping makeLetterbox(glm::vec2 regionOrigin, glm::vec2 regionSize,
                                   glm::vec2 refExtent, float zoom = 1.0f,
                                   glm::vec2 panRef = glm::vec2(0.0f));

    // ---- Hit-testing -----------------------------------------------------------

    // The 8 handle squares (in reference px) for a rect, given a handle half-size
    // expressed in reference px (so it stays a constant on-screen size, pass
    // handleHalfScreen / mapping.scale).
    std::array<glm::vec2, 8> handlePositions(const RefRect& rect);

    // Top-most handle under refPx for the selected element's rect. Corners win over
    // edges; edges over the body. grabHalfRef is the pick radius in reference px.
    HandleKind hitTestHandle(const RefRect& rect, glm::vec2 refPx, float grabHalfRef);

    // ---- Rect editing (inverse of resolvePixelRect) ----------------------------

    // Re-derive a UIRectData so its resolved rect becomes targetRef, keeping the
    // element's existing anchors + pivot fixed. Inverts UIRectMath::resolvePixelRect
    // (scale=1) — solves sizeDelta + anchoredPosition; applies the y-flip. canvasW/H
    // are the canvas reference dimensions (== refExtent for a ScaleWithScreenSize
    // canvas at its reference resolution).
    services::UIRectData solveRectData(const services::UIRectData& current,
                                       const RefRect& targetRef,
                                       float canvasW, float canvasH);

    // Apply a drag of a single handle to a starting rect, producing the new target
    // rect (reference px). Move shifts the whole rect; edge/corner handles resize
    // by moving that edge/corner while pinning the opposite one. Enforces a minimum
    // size so a rect cannot be inverted or collapsed.
    RefRect applyHandleDrag(const RefRect& startRect, HandleKind handle,
                            glm::vec2 deltaRef, float minSize = 1.0f);

    // Re-anchor: change anchorMin/anchorMax to newMin/newMax while keeping the
    // element visually fixed (its resolved rect unchanged). Back-solves sizeDelta +
    // anchoredPosition against the new anchors. canvasW/H as in solveRectData.
    services::UIRectData reanchorKeepingVisual(const services::UIRectData& current,
                                               glm::vec2 newAnchorMin, glm::vec2 newAnchorMax,
                                               float canvasW, float canvasH);

    // Forward resolve (mirror of UIRectMath::resolvePixelRect, scale=1) — exposed so
    // the editor can resolve a rect locally without crossing into utilities/scene,
    // and so the inverse functions above are self-consistent in tests.
    RefRect resolveRefRect(const services::UIRectData& data, float canvasW, float canvasH);
}
