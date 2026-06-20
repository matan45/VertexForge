#pragma once

#include "Frustum.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace math
{
    // Build a world-space frustum for a rectangular sub-region of the screen
    // (e.g. an RTS drag-select box). Reuses the engine's Gribb/Hartmann plane
    // extraction (see Frustum.hpp) so the resulting planes test identically to
    // the main camera frustum used for culling.
    //
    // Inputs:
    //   view, projection : the primary camera matrices (CameraComponent).
    //   minPx, maxPx     : the rectangle corners in viewport pixels. Any corner
    //                      ordering is accepted; the rect is normalized here.
    //   viewportW/H      : viewport size in pixels (must be > 0).
    //
    // The pixel -> NDC mapping matches RuntimePickerAdapter::screenToWorldRay:
    //   ndc = (px / dim) * 2 - 1   (no manual Y flip; the runtime projection
    //   already uses the Vulkan flipped-Y convention).
    //
    // A degenerate (zero-area) rect collapses to a sliver frustum: the planes
    // remain well-defined but enclose ~no volume, so classifyPointInFrustum
    // returns false for essentially everything. Callers that want a single-pixel
    // pick should use a ray test instead.
    inline Frustum buildScreenRegionFrustum(const glm::mat4& view,
                                            const glm::mat4& projection,
                                            const glm::vec2& minPx,
                                            const glm::vec2& maxPx,
                                            float viewportW,
                                            float viewportH)
    {
        // Normalize the rectangle so lo <= hi on both axes (handles inverted drag).
        const float loX = std::min(minPx.x, maxPx.x);
        const float hiX = std::max(minPx.x, maxPx.x);
        const float loY = std::min(minPx.y, maxPx.y);
        const float hiY = std::max(minPx.y, maxPx.y);

        const float w = (viewportW > 0.0f) ? viewportW : 1.0f;
        const float h = (viewportH > 0.0f) ? viewportH : 1.0f;

        // Pixel -> NDC, same mapping as screenToWorldRay (no Y flip).
        const float nx0 = (loX / w) * 2.0f - 1.0f;
        const float nx1 = (hiX / w) * 2.0f - 1.0f;
        const float ny0 = (loY / h) * 2.0f - 1.0f;
        const float ny1 = (hiY / h) * 2.0f - 1.0f;

        // Crop matrix mapping the NDC sub-rect [nx0,nx1]x[ny0,ny1] back to the
        // full [-1,1]^2 NDC range, leaving z untouched.
        //
        // For a zero-span axis (degenerate rect) we avoid divide-by-zero AND avoid
        // collapsing the left/right (or top/bottom) planes onto each other — that
        // would make the frustum accept the whole center column. Instead we
        // substitute a very large scale centered on the single NDC coordinate, so
        // the box becomes an infinitesimal sliver around that pixel and rejects
        // essentially every real scene point (a single-pixel region select is not
        // meaningful — callers should ray-pick for that).
        constexpr float kDegenerateScale = 1.0e6f;
        const float spanX = nx1 - nx0;
        const float spanY = ny1 - ny0;
        const bool degenX = std::abs(spanX) <= 1e-9f;
        const bool degenY = std::abs(spanY) <= 1e-9f;
        const float sx = degenX ? kDegenerateScale : (2.0f / spanX);
        const float sy = degenY ? kDegenerateScale : (2.0f / spanY);
        const float ox = degenX ? (-nx0 * kDegenerateScale) : (-(nx1 + nx0) / spanX);
        const float oy = degenY ? (-ny0 * kDegenerateScale) : (-(ny1 + ny0) / spanY);

        // glm is column-major: crop[col][row]. Scale x/y and add bias in w-row.
        glm::mat4 crop(1.0f);
        crop[0][0] = sx;
        crop[1][1] = sy;
        crop[3][0] = ox;
        crop[3][1] = oy;

        const glm::mat4 subVP = crop * projection * view;

        Frustum f;
        f.extractFromMatrix(subVP);
        return f;
    }

    // Test whether a world-space point lies inside all six frustum planes.
    inline bool classifyPointInFrustum(const Frustum& frustum, const glm::vec3& point)
    {
        return frustum.intersectsSphere(point, 0.0f);
    }
}
