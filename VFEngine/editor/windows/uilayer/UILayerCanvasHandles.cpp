#include "UILayerCanvasHandles.hpp"
#include "ui/UIRectLayout.hpp"
#include <algorithm>
#include <cmath>

namespace windows::uilayer
{
    LetterboxMapping makeLetterbox(glm::vec2 regionOrigin, glm::vec2 regionSize,
                                   glm::vec2 refExtent, float zoom, glm::vec2 panRef)
    {
        LetterboxMapping m;
        m.refExtent = refExtent;

        float refW = std::max(1.0f, refExtent.x);
        float refH = std::max(1.0f, refExtent.y);

        // Fit-to-region scale (preserve aspect), then apply zoom.
        float fit = std::min(regionSize.x / refW, regionSize.y / refH);
        if (fit <= 0.0f || !std::isfinite(fit)) fit = 1.0f;
        float scale = fit * std::max(0.01f, zoom);
        m.scale = scale;

        m.imageSizeScreen = glm::vec2(refW * scale, refH * scale);

        // Center within the region, then offset by the pan (expressed in reference px
        // so panning is resolution-independent).
        glm::vec2 centered = regionOrigin + (regionSize - m.imageSizeScreen) * 0.5f;
        m.originScreen = centered + panRef * scale;
        return m;
    }

    std::array<glm::vec2, 8> handlePositions(const RefRect& r)
    {
        float cx = r.x + r.w * 0.5f;
        float cy = r.y + r.h * 0.5f;
        // Order: TL, T, TR, R, BR, B, BL, L (matches HandleKind ordering offset).
        return {
            glm::vec2(r.x, r.y),            // TopLeft
            glm::vec2(cx, r.y),             // Top
            glm::vec2(r.right(), r.y),      // TopRight
            glm::vec2(r.right(), cy),       // Right
            glm::vec2(r.right(), r.bottom()), // BottomRight
            glm::vec2(cx, r.bottom()),      // Bottom
            glm::vec2(r.x, r.bottom()),     // BottomLeft
            glm::vec2(r.x, cy)              // Left
        };
    }

    HandleKind hitTestHandle(const RefRect& rect, glm::vec2 refPx, float grabHalfRef)
    {
        const std::array<glm::vec2, 8> pts = handlePositions(rect);
        // Corners (indices 0,2,4,6) take priority over edges (1,3,5,7).
        static constexpr HandleKind kinds[8] = {
            HandleKind::TopLeft, HandleKind::Top, HandleKind::TopRight, HandleKind::Right,
            HandleKind::BottomRight, HandleKind::Bottom, HandleKind::BottomLeft, HandleKind::Left
        };
        // First pass: corners.
        for (int i = 0; i < 8; i += 2)
        {
            if (std::abs(refPx.x - pts[i].x) <= grabHalfRef && std::abs(refPx.y - pts[i].y) <= grabHalfRef)
                return kinds[i];
        }
        // Second pass: edges.
        for (int i = 1; i < 8; i += 2)
        {
            if (std::abs(refPx.x - pts[i].x) <= grabHalfRef && std::abs(refPx.y - pts[i].y) <= grabHalfRef)
                return kinds[i];
        }
        // Body last.
        if (rect.contains(refPx))
            return HandleKind::Body;
        return HandleKind::None;
    }

    RefRect applyHandleDrag(const RefRect& s, HandleKind handle, glm::vec2 d, float minSize)
    {
        // Edges of the rect; we move the dragged edge(s) and pin the opposite ones.
        float left = s.x;
        float top = s.y;
        float right = s.right();
        float bottom = s.bottom();

        switch (handle)
        {
        case HandleKind::Body:
            left += d.x; right += d.x; top += d.y; bottom += d.y;
            break;
        case HandleKind::Left:        left += d.x; break;
        case HandleKind::Right:       right += d.x; break;
        case HandleKind::Top:         top += d.y; break;
        case HandleKind::Bottom:      bottom += d.y; break;
        case HandleKind::TopLeft:     left += d.x; top += d.y; break;
        case HandleKind::TopRight:    right += d.x; top += d.y; break;
        case HandleKind::BottomLeft:  left += d.x; bottom += d.y; break;
        case HandleKind::BottomRight: right += d.x; bottom += d.y; break;
        case HandleKind::None:        break;
        }

        // Clamp so the dragged edge can't cross its opposite (keep >= minSize). Only
        // clamp the edges that moved; Body preserves size by construction.
        if (handle != HandleKind::Body && handle != HandleKind::None)
        {
            if (right - left < minSize)
            {
                // Decide which side moved.
                if (handle == HandleKind::Left || handle == HandleKind::TopLeft || handle == HandleKind::BottomLeft)
                    left = right - minSize;
                else
                    right = left + minSize;
            }
            if (bottom - top < minSize)
            {
                if (handle == HandleKind::Top || handle == HandleKind::TopLeft || handle == HandleKind::TopRight)
                    top = bottom - minSize;
                else
                    bottom = top + minSize;
            }
        }

        return RefRect{left, top, right - left, bottom - top};
    }

    RefRect resolveRefRect(const services::UIRectData& d, float canvasW, float canvasH)
    {
        // VK-1659: the engine resolver itself (UIRectLayout.hpp, which UIRectMath::resolvePixelRect
        // delegates to) with parentW=canvasW, parentH=canvasH, scale=1.0 (ScaleWithScreenSize canvas at
        // its reference resolution). glm-only, so the editor still needs no utilities/scene/entt.
        const utilities::ui::PixelRect p =
            utilities::ui::resolveRect(utilities::ui::toRectLayout(d), canvasW, canvasH, 1.0f);
        return RefRect{p.x, p.y, p.w, p.h};
    }

    services::UIRectData solveRectData(const services::UIRectData& current,
                                       const RefRect& target, float canvasW, float canvasH)
    {
        // Invert resolveRefRect, holding anchors + pivot fixed: sizeDelta then anchoredPosition (with
        // the y-flip). VK-1659: the shared inverse (UIRectLayout.hpp); blocksRaycast is kept.
        services::UIRectData out = current;
        utilities::ui::applyRectLayout(out, utilities::ui::solveRectKeepingAnchors(
            utilities::ui::toRectLayout(current),
            utilities::ui::PixelRect{target.x, target.y, target.w, target.h}, canvasW, canvasH));
        return out;
    }

    services::UIRectData reanchorKeepingVisual(const services::UIRectData& current,
                                               glm::vec2 newAnchorMin, glm::vec2 newAnchorMax,
                                               float canvasW, float canvasH)
    {
        // Resolve the element's current visual rect, swap anchors, then re-solve
        // sizeDelta + anchoredPosition so the resolved rect is unchanged.
        RefRect visual = resolveRefRect(current, canvasW, canvasH);

        services::UIRectData out = current;
        out.anchorMin = newAnchorMin;
        out.anchorMax = newAnchorMax;
        return solveRectData(out, visual, canvasW, canvasH);
    }
}
