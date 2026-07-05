#ifndef VT_TYPES_GLSL
#define VT_TYPES_GLSL

// ============================================================
// Virtual Texturing (VK-1209) — GLSL mirror of
// VFEngine/graphics/render/virtualtexture/VTTypes.hpp. Keep in lockstep: the C++
// side is unit-tested (test_vt_addressing) and parity here is by construction,
// the same contract shadow_sampling.glsl has with VSMTypes.hpp.
// ============================================================

const uint VT_PAGE_SIZE = 128u;
const uint VT_BORDER = 4u;
const uint VT_PAGE_INTERIOR = VT_PAGE_SIZE - 2u * VT_BORDER; // 120

const uint VT_ENTRY_VALID_BIT = 0x80000000u;
const uint VT_ENTRY_XY_MASK = 0xFFFu; // 12 bits
const uint VT_ENTRY_Y_SHIFT = 12u;

// Per-virtual-image descriptor. 8 uints = 32 bytes; std140/std430-friendly.
// Mirrors VTImageDesc + poolDim so a shader can resolve entirely from this struct.
struct VTImageInfo
{
    uint pagesX0;
    uint pagesY0;
    uint mipCount;
    uint pageTableBase;
    uint poolDim;       // physical atlas edge in texels
    uint pad0;
    uint pad1;
    uint pad2;
};

// pages spanning one axis at mip m = ceil(pages0 / 2^m), never below 1.
// MUST match render::vt::vtPagesAtMip.
uint vtPagesAtMip(uint pages0, uint mip)
{
    uint denom = 1u << mip;
    uint p = (pages0 + denom - 1u) / denom;
    return max(p, 1u);
}

// Entry index (within an image block) where mip `mip` begins. Matches vtMipSubOffset.
uint vtMipSubOffset(uint pagesX0, uint pagesY0, uint mip)
{
    uint off = 0u;
    for (uint m = 0u; m < mip; ++m)
        off += vtPagesAtMip(pagesX0, m) * vtPagesAtMip(pagesY0, m);
    return off;
}

bool vtIsPageValid(uint entry) { return (entry & VT_ENTRY_VALID_BIT) != 0u; }
uint vtEntryTileX(uint entry) { return entry & VT_ENTRY_XY_MASK; }
uint vtEntryTileY(uint entry) { return (entry >> VT_ENTRY_Y_SHIFT) & VT_ENTRY_XY_MASK; }

#endif // VT_TYPES_GLSL
