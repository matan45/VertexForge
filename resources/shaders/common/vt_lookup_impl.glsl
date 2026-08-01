// ============================================================
// Virtual Texturing — the virtual->physical walk, as an INSTANTIABLE body.
//
// DELIBERATELY NOT INCLUDE-GUARDED. This file is included once per virtual-texture
// INSTANCE, and each inclusion emits one lookup function bound to the page-table
// SSBO named by VT_PAGE_TABLE at include time. vt_sampling.glsl includes it for the
// first instance under the default name `vtLookup`; a shader that hosts a SECOND
// virtual texture (VK-1620: the scene mesh shader carries material SVT *and* the
// terrain RVT) redefines VT_PAGE_TABLE and VT_LOOKUP_FN and includes it again.
//
// Consumers must define, before including:
//   VT_PAGE_TABLE  <name>   // a `uint <name>[];` SSBO member
//   VT_LOOKUP_FN   <name>   // the function name to emit
// Optional, to emit a feedback writer for this instance:
//   VT_FEEDBACK          <name>   // a `uint <name>[];` SSBO member (atomicOr target)
//   VT_WRITE_FEEDBACK_FN <name>   // the function name to emit
//
// VT_LOOKUP_FN / VT_WRITE_FEEDBACK_FN are #undef'd on the way out so the next
// instance cannot silently inherit the previous instance's name. VT_PAGE_TABLE and
// VT_FEEDBACK are left alone — they belong to the consumer, and once a body has been
// emitted the macro has already been substituted into it, so redefining them later
// cannot disturb an instance that was already generated.
//
// Object-like macro substitution in the declarator is used rather than `##` token
// pasting: glslang does not reliably implement the pasting operator.
// ============================================================

#include "vt_types.glsl"

#ifndef VT_PAGE_TABLE
#error "vt_lookup_impl.glsl requires VT_PAGE_TABLE to name a uint[] SSBO member"
#endif
#ifndef VT_LOOKUP_FN
#error "vt_lookup_impl.glsl requires VT_LOOKUP_FN to name the function to emit"
#endif

// Walks from the desired (finest) mip toward coarser mips until a resident page is
// found, then maps the logical page UV into that tile's bordered interior. With the
// coarsest mip pinned resident, a lookup always resolves — a streaming page reads
// briefly blurry, never as a hole.
VTSample VT_LOOKUP_FN(VTImageInfo img, vec2 uv, uint desiredMip)
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

#if defined(VT_FEEDBACK) && defined(VT_WRITE_FEEDBACK_FN)
// Mark page (mip, x, y) as requested this frame (idempotent atomicOr into the bitmask).
void VT_WRITE_FEEDBACK_FN(VTImageInfo img, vec2 uv, uint mip)
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
    // Bit-packed feedback: one bit per entry, 32 entries per uint word (VK-1480).
    // Mirrors render::vt::VTFeedbackWords.hpp — CPU decode must use the same packing.
    atomicOr(VT_FEEDBACK[entryIdx >> 5u], 1u << (entryIdx & 31u));
}
#endif

#undef VT_LOOKUP_FN
#undef VT_WRITE_FEEDBACK_FN
