#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace render::shadow
{
    // Inclusive range of virtual-shadow-map pages [fx0,fx1] x [fy0,fy1] that a projected
    // bounding box can touch within one shadow view's page grid. valid == false means the
    // box projects entirely outside the grid (no pages).
    struct PageRange
    {
        uint32_t fx0 = 0;
        uint32_t fy0 = 0;
        uint32_t fx1 = 0;
        uint32_t fy1 = 0;
        bool valid = false;
    };

    // Pure, header-only page-range projection shared by the CPU dynamic-page marking
    // (ShadowSystem::determineDynamicPages) and — mirrored statement-for-statement in
    // resources/shaders/common/shadow_page_overlap.glsl — the page-binned shadow cull.
    // No Vulkan dependency so the CPU-only Tests project validates it (test_shadow_page_overlap)
    // and the GLSL mirror stays in lockstep. Follows the DirectionalShadowCalculator pattern.
    //
    // The page grid maps clip space to pages the same way computePageCropMatrix (VSMTypes.hpp)
    // maps pages to NDC: page fx spans NDC x in [2*fx/pagesX - 1, 2*(fx+1)/pagesX - 1], so a
    // point at NDC x lands in page fx = floor((ndc.x*0.5 + 0.5) * pagesX).
    class ShadowPageOverlap
    {
    public:
        // clipFromBox maps the box's local space to clip space (e.g. lightViewProjection for a
        // world-space AABB, or lightViewProjection * modelMatrix for a mesh-local AABB — the
        // latter is tighter since it avoids re-boxing the world AABB). Ortho clipmap views have
        // w == 1 for every corner, giving an exact 2D interval; perspective spot/point views fall
        // back to the full grid when the box straddles the near plane (w <= eps for some corner).
        static PageRange overlappedPages(const glm::vec3& boxMin,
                                         const glm::vec3& boxMax,
                                         const glm::mat4& clipFromBox,
                                         uint32_t pagesX, uint32_t pagesY)
        {
            PageRange range;
            if (pagesX == 0 || pagesY == 0)
                return range;

            constexpr float kWEps = 1e-6f;
            float minX = 1e30f, minY = 1e30f;
            float maxX = -1e30f, maxY = -1e30f;
            bool anyBehind = false;
            bool anyFront = false;

            for (int corner = 0; corner < 8; ++corner)
            {
                glm::vec4 p(
                    (corner & 1) ? boxMax.x : boxMin.x,
                    (corner & 2) ? boxMax.y : boxMin.y,
                    (corner & 4) ? boxMax.z : boxMin.z,
                    1.0f);
                glm::vec4 clip = clipFromBox * p;
                if (clip.w <= kWEps)
                {
                    anyBehind = true;
                    continue;
                }
                anyFront = true;
                float ndcX = clip.x / clip.w;
                float ndcY = clip.y / clip.w;
                minX = std::min(minX, ndcX);
                maxX = std::max(maxX, ndcX);
                minY = std::min(minY, ndcY);
                maxY = std::max(maxY, ndcY);
            }

            if (anyBehind)
            {
                // Fully behind the light: casts into no page. Straddling the near plane: the
                // projected hull is unreliable, so conservatively claim the whole grid.
                if (!anyFront)
                    return range;
                range.fx0 = 0;
                range.fy0 = 0;
                range.fx1 = pagesX - 1;
                range.fy1 = pagesY - 1;
                range.valid = true;
                return range;
            }
            if (!anyFront)
                return range;

            // Entirely outside NDC on any axis -> touches no page.
            if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f)
                return range;

            const int32_t lastX = static_cast<int32_t>(pagesX) - 1;
            const int32_t lastY = static_cast<int32_t>(pagesY) - 1;

            int32_t px0 = static_cast<int32_t>(std::floor((minX * 0.5f + 0.5f) * static_cast<float>(pagesX)));
            int32_t px1 = static_cast<int32_t>(std::floor((maxX * 0.5f + 0.5f) * static_cast<float>(pagesX)));
            int32_t py0 = static_cast<int32_t>(std::floor((minY * 0.5f + 0.5f) * static_cast<float>(pagesY)));
            int32_t py1 = static_cast<int32_t>(std::floor((maxY * 0.5f + 0.5f) * static_cast<float>(pagesY)));

            range.fx0 = static_cast<uint32_t>(std::clamp(px0, 0, lastX));
            range.fx1 = static_cast<uint32_t>(std::clamp(px1, 0, lastX));
            range.fy0 = static_cast<uint32_t>(std::clamp(py0, 0, lastY));
            range.fy1 = static_cast<uint32_t>(std::clamp(py1, 0, lastY));
            range.valid = true;
            return range;
        }
    };
}
