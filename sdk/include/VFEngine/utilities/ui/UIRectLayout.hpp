#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string_view>

// VK-1659: the UI rect math every UI path shares — the runtime resolver (rendering + hit tests), the
// edit-mode world-space canvas preview, the UI Layer Builder and the MCP UI tools. Pure: glm only, any
// thread, header-only.
//
// Conventions of resolveRect (the engine's one resolver; UIRectMath.hpp resolvePixelRect delegates to it):
//   - anchors are fractions of the WHOLE canvas viewport, never of the parent element, and are y up;
//   - sizeDelta and anchoredPosition are canvas reference pixels, multiplied by the canvas scale;
//     anchoredPosition.y is up;
//   - pivot.x is measured from the left edge and pivot.y from the TOP edge (resolved pixels are y down).
// A child therefore never follows its parent; placeInside() below glues a child to a parent's rect instead,
// by giving it the parent's anchor terms.
namespace utilities::ui
{
    struct PixelRect
    {
        float x, y, w, h;
    };

    // The authored fields of a UIRectComponent / services::UIRectData.
    struct RectLayout
    {
        glm::vec2 anchorMin{0.0f, 0.0f};
        glm::vec2 anchorMax{1.0f, 1.0f};
        glm::vec2 pivot{0.5f, 0.5f};
        glm::vec2 sizeDelta{0.0f, 0.0f};
        glm::vec2 anchoredPosition{0.0f, 0.0f};
    };

    // R = components::UIRectComponent or services::UIRectData (same member names).
    template <typename R>
    RectLayout toRectLayout(const R& rect)
    {
        RectLayout out;
        out.anchorMin = rect.anchorMin;
        out.anchorMax = rect.anchorMax;
        out.pivot = rect.pivot;
        out.sizeDelta = rect.sizeDelta;
        out.anchoredPosition = rect.anchoredPosition;
        return out;
    }

    // Writes the five layout fields; every other member of `rect` (blocksRaycast) is kept.
    template <typename R>
    void applyRectLayout(R& rect, const RectLayout& layout)
    {
        rect.anchorMin = layout.anchorMin;
        rect.anchorMax = layout.anchorMax;
        rect.pivot = layout.pivot;
        rect.sizeDelta = layout.sizeDelta;
        rect.anchoredPosition = layout.anchoredPosition;
    }

    // The element's rect in resolved pixels (top-left origin, y down) for a parentW x parentH viewport at
    // canvas scale `scale`.
    inline PixelRect resolveRect(const RectLayout& rectComp, float parentW, float parentH, float scale)
    {
        float anchorLeftPx  = rectComp.anchorMin.x * parentW;
        float anchorRightPx = rectComp.anchorMax.x * parentW;
        float anchorTopPx   = (1.0f - rectComp.anchorMax.y) * parentH;
        float anchorBotPx   = (1.0f - rectComp.anchorMin.y) * parentH;

        float w = (anchorRightPx - anchorLeftPx) + rectComp.sizeDelta.x * scale;
        float h = (anchorBotPx - anchorTopPx) + rectComp.sizeDelta.y * scale;

        float cx = (anchorLeftPx + anchorRightPx) * 0.5f + rectComp.anchoredPosition.x * scale;
        float cy = (anchorTopPx + anchorBotPx) * 0.5f - rectComp.anchoredPosition.y * scale;

        float posX = cx - rectComp.pivot.x * w;
        float posY = cy - rectComp.pivot.y * h;

        return {posX, posY, w, h};
    }

    // The inverse of resolveRect at scale 1 (a canvas at its reference resolution) holding anchors and pivot
    // fixed: the sizeDelta and anchoredPosition that make `current` resolve to `target`.
    inline RectLayout solveRectKeepingAnchors(const RectLayout& current, const PixelRect& target,
                                              float canvasW, float canvasH)
    {
        RectLayout out = current;

        float anchorLeftPx = current.anchorMin.x * canvasW;
        float anchorRightPx = current.anchorMax.x * canvasW;
        float anchorTopPx = (1.0f - current.anchorMax.y) * canvasH;
        float anchorBotPx = (1.0f - current.anchorMin.y) * canvasH;

        float anchorW = anchorRightPx - anchorLeftPx;
        float anchorH = anchorBotPx - anchorTopPx;

        // w = anchorW + sizeDelta.x  ->  sizeDelta.x = w - anchorW
        out.sizeDelta.x = target.w - anchorW;
        out.sizeDelta.y = target.h - anchorH;

        // posX = cx - pivot.x*w  ->  cx = posX + pivot.x*w
        float cx = target.x + current.pivot.x * target.w;
        float cy = target.y + current.pivot.y * target.h;

        // cx = anchorMid + anchoredPosition.x  ->  anchoredPosition.x = cx - anchorMid
        out.anchoredPosition.x = cx - (anchorLeftPx + anchorRightPx) * 0.5f;
        // cy = anchorMid - anchoredPosition.y  ->  anchoredPosition.y = anchorMid - cy
        out.anchoredPosition.y = (anchorTopPx + anchorBotPx) * 0.5f - cy;

        return out;
    }

    // ------------------------------------------------------------------
    // Affine edges: each edge of a resolved rect is f * extent + c * scale, where extent is the viewport
    // width (left / right) or height (top / bottom) and scale the canvas scale. x right, y DOWN.
    // ------------------------------------------------------------------

    struct AffineEdge
    {
        float f = 0.0f;  // fraction of the viewport extent
        float c = 0.0f;  // canvas reference pixels (multiplied by the canvas scale)
    };

    struct AffineRect
    {
        AffineEdge left;
        AffineEdge right;
        AffineEdge top;
        AffineEdge bottom;
    };

    // The edges of `r` as resolveRect computes them, written as an exact algebraic rewrite.
    inline AffineRect affineRectOf(const RectLayout& r)
    {
        const float midX = (r.anchorMin.x + r.anchorMax.x) * 0.5f;
        const float spanX = r.anchorMax.x - r.anchorMin.x;
        const float midYDown = 1.0f - (r.anchorMin.y + r.anchorMax.y) * 0.5f;
        const float spanY = r.anchorMax.y - r.anchorMin.y;

        AffineRect out;
        out.left = {midX - r.pivot.x * spanX, r.anchoredPosition.x - r.pivot.x * r.sizeDelta.x};
        out.right = {midX + (1.0f - r.pivot.x) * spanX, r.anchoredPosition.x + (1.0f - r.pivot.x) * r.sizeDelta.x};
        out.top = {midYDown - r.pivot.y * spanY, -r.anchoredPosition.y - r.pivot.y * r.sizeDelta.y};
        out.bottom = {midYDown + (1.0f - r.pivot.y) * spanY, -r.anchoredPosition.y + (1.0f - r.pivot.y) * r.sizeDelta.y};
        return out;
    }

    // The whole viewport: a canvas root, or a parent that has no UIRect.
    inline AffineRect fullViewportRect()
    {
        AffineRect out;
        out.left = {0.0f, 0.0f};
        out.right = {1.0f, 0.0f};
        out.top = {0.0f, 0.0f};
        out.bottom = {1.0f, 0.0f};
        return out;
    }

    // The layout (pivot 0.5, 0.5) whose resolveRect has exactly the edges `a`: affineRectOf(rectFromEdges(a))
    // reproduces `a`.
    inline RectLayout rectFromEdges(const AffineRect& a)
    {
        RectLayout r;
        r.anchorMin = {a.left.f, 1.0f - a.bottom.f};
        r.anchorMax = {a.right.f, 1.0f - a.top.f};
        r.pivot = {0.5f, 0.5f};
        r.sizeDelta = {a.right.c - a.left.c, a.bottom.c - a.top.c};
        r.anchoredPosition = {(a.left.c + a.right.c) * 0.5f, -(a.top.c + a.bottom.c) * 0.5f};
        return r;
    }

    // The resolved rect of affine edges for a W x H viewport at canvas scale `s` (= resolveRect up to float
    // rounding).
    inline PixelRect evaluate(const AffineRect& a, float W, float H, float s)
    {
        const float left = a.left.f * W + a.left.c * s;
        const float right = a.right.f * W + a.right.c * s;
        const float top = a.top.f * H + a.top.c * s;
        const float bottom = a.bottom.f * H + a.bottom.c * s;
        return {left, top, right - left, bottom - top};
    }

    // ------------------------------------------------------------------
    // Placement: a preset + size + margin inside a parent's edges
    // ------------------------------------------------------------------

    enum class HAlign : uint8_t
    {
        Left,
        Center,
        Right,
        Stretch
    };

    enum class VAlign : uint8_t
    {
        Top,
        Middle,
        Bottom,
        Stretch
    };

    struct AnchorPreset
    {
        std::string_view name;
        HAlign h;
        VAlign v;
    };

    // stretchTop / stretchMiddle / stretchBottom span the full width; stretchLeft / stretchCenter /
    // stretchRight the full height; stretch both.
    inline constexpr std::array<AnchorPreset, 16> anchorPresets{{
        {"topLeft", HAlign::Left, VAlign::Top},
        {"top", HAlign::Center, VAlign::Top},
        {"topRight", HAlign::Right, VAlign::Top},
        {"left", HAlign::Left, VAlign::Middle},
        {"center", HAlign::Center, VAlign::Middle},
        {"right", HAlign::Right, VAlign::Middle},
        {"bottomLeft", HAlign::Left, VAlign::Bottom},
        {"bottom", HAlign::Center, VAlign::Bottom},
        {"bottomRight", HAlign::Right, VAlign::Bottom},
        {"stretchTop", HAlign::Stretch, VAlign::Top},
        {"stretchMiddle", HAlign::Stretch, VAlign::Middle},
        {"stretchBottom", HAlign::Stretch, VAlign::Bottom},
        {"stretchLeft", HAlign::Left, VAlign::Stretch},
        {"stretchCenter", HAlign::Center, VAlign::Stretch},
        {"stretchRight", HAlign::Right, VAlign::Stretch},
        {"stretch", HAlign::Stretch, VAlign::Stretch},
    }};

    struct Placement
    {
        HAlign h = HAlign::Center;
        VAlign v = VAlign::Middle;
        glm::vec2 size{100.0f, 100.0f};  // reference px; ignored on a stretched axis
        glm::vec4 margin{0.0f};          // left, right, top, bottom: inward, reference px (the engine's padding order);
                                         // an edge the preset does not anchor to ignores its margin
        glm::vec2 offset{0.0f};          // reference px added after placement, +y DOWN
    };

    // The layout (pivot 0.5, 0.5) of a child placed inside `parent`. The child takes the parent's anchor
    // terms, so it keeps its place inside the parent at every viewport size and canvas scale. Sizes, margins
    // and offsets scale with the canvas like every other reference-pixel value.
    inline RectLayout placeInside(const AffineRect& parent, const Placement& p)
    {
        AffineRect out;

        switch (p.h)
        {
        case HAlign::Left:
            out.left = {parent.left.f, parent.left.c + p.margin.x};
            out.right = {out.left.f, out.left.c + p.size.x};
            break;
        case HAlign::Right:
            out.right = {parent.right.f, parent.right.c - p.margin.y};
            out.left = {out.right.f, out.right.c - p.size.x};
            break;
        case HAlign::Center:
        {
            const AffineEdge centre{(parent.left.f + parent.right.f) * 0.5f, (parent.left.c + parent.right.c) * 0.5f};
            out.left = {centre.f, centre.c - p.size.x * 0.5f};
            out.right = {centre.f, centre.c + p.size.x * 0.5f};
            break;
        }
        case HAlign::Stretch:
            out.left = {parent.left.f, parent.left.c + p.margin.x};
            out.right = {parent.right.f, parent.right.c - p.margin.y};
            break;
        }

        switch (p.v)
        {
        case VAlign::Top:
            out.top = {parent.top.f, parent.top.c + p.margin.z};
            out.bottom = {out.top.f, out.top.c + p.size.y};
            break;
        case VAlign::Bottom:
            out.bottom = {parent.bottom.f, parent.bottom.c - p.margin.w};
            out.top = {out.bottom.f, out.bottom.c - p.size.y};
            break;
        case VAlign::Middle:
        {
            const AffineEdge centre{(parent.top.f + parent.bottom.f) * 0.5f, (parent.top.c + parent.bottom.c) * 0.5f};
            out.top = {centre.f, centre.c - p.size.y * 0.5f};
            out.bottom = {centre.f, centre.c + p.size.y * 0.5f};
            break;
        }
        case VAlign::Stretch:
            out.top = {parent.top.f, parent.top.c + p.margin.z};
            out.bottom = {parent.bottom.f, parent.bottom.c - p.margin.w};
            break;
        }

        out.left.c += p.offset.x;
        out.right.c += p.offset.x;
        out.top.c += p.offset.y;
        out.bottom.c += p.offset.y;
        return rectFromEdges(out);
    }

    // ------------------------------------------------------------------
    // Moving: a resolved move of frac * viewport + offset * scale (x right, y DOWN)
    // ------------------------------------------------------------------

    struct AffineDelta
    {
        glm::vec2 frac{0.0f};
        glm::vec2 offset{0.0f};
    };

    // How far the centre of a rect moved from `before` to `after`.
    inline AffineDelta centreDelta(const AffineRect& before, const AffineRect& after)
    {
        AffineDelta d;
        d.frac.x = ((after.left.f + after.right.f) - (before.left.f + before.right.f)) * 0.5f;
        d.offset.x = ((after.left.c + after.right.c) - (before.left.c + before.right.c)) * 0.5f;
        d.frac.y = ((after.top.f + after.bottom.f) - (before.top.f + before.bottom.f)) * 0.5f;
        d.offset.y = ((after.top.c + after.bottom.c) - (before.top.c + before.bottom.c)) * 0.5f;
        return d;
    }

    // `r` moved by `d` at every viewport size; its size is unchanged.
    inline RectLayout translateRect(RectLayout r, const AffineDelta& d)
    {
        r.anchorMin.x += d.frac.x;
        r.anchorMax.x += d.frac.x;
        r.anchoredPosition.x += d.offset.x;
        // y down in the affine frame, y up in the layout
        r.anchorMin.y -= d.frac.y;
        r.anchorMax.y -= d.frac.y;
        r.anchoredPosition.y -= d.offset.y;
        return r;
    }

    // ------------------------------------------------------------------
    // Edit-mode world-space canvas
    // ------------------------------------------------------------------

    // The element's rect in canvas reference pixels with the origin at the canvas' BOTTOM-left and y up: the
    // frame of the edit-mode world-space canvas quad. It is resolveRect at the reference resolution (scale
    // 1), so the edit-mode preview places every element (any pivot) exactly where Play draws it.
    struct CanvasRectYUp
    {
        float left = 0.0f;
        float bottom = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    inline CanvasRectYUp canvasRectYUp(const RectLayout& r, float refW, float refH)
    {
        const PixelRect p = resolveRect(r, refW, refH, 1.0f);
        return {p.x, refH - (p.y + p.h), p.w, p.h};
    }
}
