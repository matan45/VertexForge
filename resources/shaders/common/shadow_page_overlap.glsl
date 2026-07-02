#ifndef SHADOW_PAGE_OVERLAP_GLSL
#define SHADOW_PAGE_OVERLAP_GLSL

// GLSL mirror of render::shadow::ShadowPageOverlap::overlappedPages (VFEngine/graphics/render/
// shadow/ShadowPageOverlap.hpp). Keep this statement-for-statement identical to the C++ header:
// test_shadow_page_overlap validates the CPU side, and the GLSL/CPU parity is what lets the
// page-binned shadow cull (B1) rely on the same page ranges the CPU marking uses.
//
// clipFromBox maps the box's local space to clip space (lightViewProjection, or *modelMatrix for a
// mesh-local AABB). Ortho clipmap views have w == 1 for every corner (exact interval); perspective
// spot/point views fall back to the whole grid when the box straddles the near plane.

struct PageRange
{
    uint fx0;
    uint fy0;
    uint fx1;
    uint fy1;
    bool valid;
};

PageRange overlappedPages(vec3 boxMin, vec3 boxMax, mat4 clipFromBox, uint pagesX, uint pagesY)
{
    PageRange range;
    range.fx0 = 0u; range.fy0 = 0u; range.fx1 = 0u; range.fy1 = 0u; range.valid = false;
    if (pagesX == 0u || pagesY == 0u)
        return range;

    const float kWEps = 1e-6;
    float minX = 1e30, minY = 1e30;
    float maxX = -1e30, maxY = -1e30;
    bool anyBehind = false;
    bool anyFront = false;

    for (int corner = 0; corner < 8; ++corner)
    {
        vec4 p = vec4(
            ((corner & 1) != 0) ? boxMax.x : boxMin.x,
            ((corner & 2) != 0) ? boxMax.y : boxMin.y,
            ((corner & 4) != 0) ? boxMax.z : boxMin.z,
            1.0);
        vec4 clip = clipFromBox * p;
        if (clip.w <= kWEps)
        {
            anyBehind = true;
            continue;
        }
        anyFront = true;
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        minX = min(minX, ndcX);
        maxX = max(maxX, ndcX);
        minY = min(minY, ndcY);
        maxY = max(maxY, ndcY);
    }

    if (anyBehind)
    {
        if (!anyFront)
            return range;
        range.fx0 = 0u;
        range.fy0 = 0u;
        range.fx1 = pagesX - 1u;
        range.fy1 = pagesY - 1u;
        range.valid = true;
        return range;
    }
    if (!anyFront)
        return range;

    if (maxX < -1.0 || minX > 1.0 || maxY < -1.0 || minY > 1.0)
        return range;

    int lastX = int(pagesX) - 1;
    int lastY = int(pagesY) - 1;

    int px0 = int(floor((minX * 0.5 + 0.5) * float(pagesX)));
    int px1 = int(floor((maxX * 0.5 + 0.5) * float(pagesX)));
    int py0 = int(floor((minY * 0.5 + 0.5) * float(pagesY)));
    int py1 = int(floor((maxY * 0.5 + 0.5) * float(pagesY)));

    range.fx0 = uint(clamp(px0, 0, lastX));
    range.fx1 = uint(clamp(px1, 0, lastX));
    range.fy0 = uint(clamp(py0, 0, lastY));
    range.fy1 = uint(clamp(py1, 0, lastY));
    range.valid = true;
    return range;
}

#endif // SHADOW_PAGE_OVERLAP_GLSL
