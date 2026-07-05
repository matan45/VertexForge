#ifndef VT_SAMPLING_GLSL
#define VT_SAMPLING_GLSL

#include "vt_types.glsl"

// ============================================================
// Virtual Texturing (VK-1209) — virtual->physical translation, GLSL mirror of
// render::vt::VTAddressing (VTAddressing.hpp). Walks from the desired (finest) mip
// toward coarser mips until a resident page is found, then maps the logical page
// UV into that tile's bordered interior. With the coarsest mip pinned resident, a
// lookup always resolves — a streaming page reads briefly blurry, never as a hole.
//
// Requires the consumer to declare, before #include:
//   #define VT_PAGE_TABLE <name>   // a `uint <name>[];` SSBO member
// Optional, to emit page requests:
//   #define VT_FEEDBACK   <name>   // a `uint <name>[];` SSBO member (atomicOr target)
// ============================================================

struct VTSample
{
    vec2 uv;           // physical atlas UV [0,1)
    uint residentMip;  // mip actually found (>= desired)
    bool valid;
};

#ifdef VT_PAGE_TABLE
VTSample vtLookup(VTImageInfo img, vec2 uv, uint desiredMip)
{
    VTSample s;
    s.uv = vec2(0.0);
    s.residentMip = 0u;
    s.valid = false;
    if (img.mipCount == 0u)
        return s;

    uv = clamp(uv, vec2(0.0), vec2(0.999999));
    uint mip = min(desiredMip, img.mipCount - 1u);
    // Compute the starting mip's block offset once, then advance it incrementally per level
    // (O(mip) total instead of recomputing vtMipSubOffset every iteration = O(mip^2)).
    uint mipOffset = vtMipSubOffset(img.pagesX0, img.pagesY0, mip);

    for (; mip < img.mipCount; ++mip)
    {
        uint pagesX = vtPagesAtMip(img.pagesX0, mip);
        uint pagesY = vtPagesAtMip(img.pagesY0, mip);

        uint pageX = min(uint(uv.x * float(pagesX)), pagesX - 1u);
        uint pageY = min(uint(uv.y * float(pagesY)), pagesY - 1u);

        uint entry = VT_PAGE_TABLE[img.pageTableBase + mipOffset + pageY * pagesX + pageX];
        if (vtIsPageValid(entry))
        {
            uint tileX = vtEntryTileX(entry);
            uint tileY = vtEntryTileY(entry);

            float fx = uv.x * float(pagesX) - float(pageX);
            float fy = uv.y * float(pagesY) - float(pageY);

            float texelX = float(tileX * VT_PAGE_SIZE + VT_BORDER) + fx * float(VT_PAGE_INTERIOR);
            float texelY = float(tileY * VT_PAGE_SIZE + VT_BORDER) + fy * float(VT_PAGE_INTERIOR);

            s.uv = vec2(texelX, texelY) / float(img.poolDim);
            s.residentMip = mip;
            s.valid = true;
            return s;
        }
        mipOffset += pagesX * pagesY; // next coarser level's block offset
    }
    return s;
}
#endif // VT_PAGE_TABLE

// Desired mip from screen-space UV derivatives (fragment stage). virtualResTexels
// is the mip-0 texel resolution along the sampled axis. Mirrors
// VTAddressing::desiredMipFromDerivatives.
float vtDesiredMip(vec2 uv, float virtualResTexels)
{
    vec2 dx = dFdx(uv) * virtualResTexels;
    vec2 dy = dFdy(uv) * virtualResTexels;
    float m = max(length(dx), length(dy));
    return (m <= 1.0) ? 0.0 : log2(m);
}

// Feedback need only be emitted by a fraction of fragments: a visible page covers many pixels, so
// a few requests suffice, and full-rate atomicOr from every fragment causes heavy contention (all
// fragments of one page hit the same entry). ~1/64 of fragments (every 8th in x and y).
bool vtFeedbackFragment(vec2 fragCoord)
{
    return (uint(fragCoord.x) & 7u) == 0u && (uint(fragCoord.y) & 7u) == 0u;
}

#ifdef VT_FEEDBACK
// Mark page (mip, x, y) as requested this frame (idempotent atomicOr into the bitmask).
void vtWriteFeedback(VTImageInfo img, vec2 uv, uint mip)
{
    if (img.mipCount == 0u)
        return;
    uv = clamp(uv, vec2(0.0), vec2(0.999999));
    mip = min(mip, img.mipCount - 1u);
    uint pagesX = vtPagesAtMip(img.pagesX0, mip);
    uint pagesY = vtPagesAtMip(img.pagesY0, mip);
    uint pageX = min(uint(uv.x * float(pagesX)), pagesX - 1u);
    uint pageY = min(uint(uv.y * float(pagesY)), pagesY - 1u);
    uint entryIdx = img.pageTableBase
                  + vtMipSubOffset(img.pagesX0, img.pagesY0, mip)
                  + pageY * pagesX + pageX;
    atomicOr(VT_FEEDBACK[entryIdx], 1u);
}
#endif // VT_FEEDBACK

#endif // VT_SAMPLING_GLSL
