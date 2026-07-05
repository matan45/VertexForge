#pragma once

#include <cstdint>
#include <cmath>

// ============================================================================
// Virtual Texturing (VK-1209) — shared page-table constants, entry packing, and
// mip-pyramid addressing math. PURE / header-only / no Vulkan so the CPU-only
// Tests project validates it (test_vt_addressing) and the GLSL mirror
// (resources/shaders/common/vt_types.glsl) stays in lockstep — the same pattern
// VSM uses with VSMTypes.hpp + shadow_sampling.glsl.
//
// A "virtual image" is a full mip pyramid of pages. Page (mip, x, y) maps through
// the page table to a physical tile in a shared atlas ("pool"). Unlike VSM's entry
// (6+6 tile bits, no mip — mip picked by table offset), VT still addresses the mip
// via the table sub-offset, but widens the tile fields to 12+12 bits so a
// budget-scaled pool can exceed 64 tiles per side.
// ============================================================================

namespace render::vt
{
    // Physical tile edge in texels. A page's usable content is VT_PAGE_INTERIOR
    // texels; the remaining VT_BORDER texels on each side hold replicated neighbour
    // content so hardware bilinear/anisotropic taps near a page edge stay correct.
    inline constexpr uint32_t VT_PAGE_SIZE = 128;
    inline constexpr uint32_t VT_BORDER = 4;
    inline constexpr uint32_t VT_PAGE_INTERIOR = VT_PAGE_SIZE - 2u * VT_BORDER; // 120

    inline constexpr uint32_t VT_INVALID_TILE = 0xFFFFFFFFu;
    inline constexpr uint32_t VT_MAX_MIPS = 16;

    // --- Page table entry (packed uint32) -----------------------------------
    // Bits [0..11]:  physical tile X (0..4095)
    // Bits [12..23]: physical tile Y (0..4095)
    // Bit  [31]:     valid flag
    inline constexpr uint32_t VT_ENTRY_VALID_BIT = 0x80000000u;
    inline constexpr uint32_t VT_ENTRY_XY_MASK = 0xFFFu; // 12 bits
    inline constexpr uint32_t VT_ENTRY_Y_SHIFT = 12;

    inline uint32_t vtPackPageEntry(uint32_t tileX, uint32_t tileY)
    {
        return VT_ENTRY_VALID_BIT | (tileX & VT_ENTRY_XY_MASK) | ((tileY & VT_ENTRY_XY_MASK) << VT_ENTRY_Y_SHIFT);
    }

    inline void vtUnpackPageEntry(uint32_t entry, uint32_t& tileX, uint32_t& tileY)
    {
        tileX = entry & VT_ENTRY_XY_MASK;
        tileY = (entry >> VT_ENTRY_Y_SHIFT) & VT_ENTRY_XY_MASK;
    }

    inline bool vtIsPageValid(uint32_t entry)
    {
        return (entry & VT_ENTRY_VALID_BIT) != 0u;
    }

    // --- Mip-pyramid addressing ---------------------------------------------
    // Pages spanning one axis at mip m = ceil(pages0 / 2^m), never below 1. Ceil
    // (not floor) so a coarser level always fully covers the image and the pyramid
    // terminates cleanly at a single page. The GLSL mirror MUST use the same rule.
    inline uint32_t vtPagesAtMip(uint32_t pages0, uint32_t mip)
    {
        const uint32_t denom = 1u << mip;
        const uint32_t p = (pages0 + denom - 1u) / denom; // ceil
        return p < 1u ? 1u : p;
    }

    // Number of mip levels until max(pagesX0,pagesY0) collapses to a single page.
    inline uint32_t vtComputeMipCount(uint32_t pagesX0, uint32_t pagesY0)
    {
        uint32_t p = pagesX0 > pagesY0 ? pagesX0 : pagesY0;
        if (p < 1u) p = 1u;
        uint32_t mips = 1u;
        while (p > 1u)
        {
            p = (p + 1u) / 2u; // ceil halving, mirrors vtPagesAtMip stepping
            ++mips;
        }
        return mips;
    }

    // Entry index (within an image's page-table block) where mip `mip` begins.
    inline uint32_t vtMipSubOffset(uint32_t pagesX0, uint32_t pagesY0, uint32_t mip)
    {
        uint32_t off = 0;
        for (uint32_t m = 0; m < mip; ++m)
            off += vtPagesAtMip(pagesX0, m) * vtPagesAtMip(pagesY0, m);
        return off;
    }

    // Total entries an image's full pyramid occupies in the page table.
    inline uint32_t vtBlockEntryCount(uint32_t pagesX0, uint32_t pagesY0, uint32_t mipCount)
    {
        uint32_t total = 0;
        for (uint32_t m = 0; m < mipCount; ++m)
            total += vtPagesAtMip(pagesX0, m) * vtPagesAtMip(pagesY0, m);
        return total;
    }

    // Linear entry index of page (mip, x, y) within an image's block (row-major per mip).
    inline uint32_t vtPageLinearIndex(uint32_t pagesX0, uint32_t pagesY0, uint32_t mip, uint32_t x, uint32_t y)
    {
        return vtMipSubOffset(pagesX0, pagesY0, mip) + y * vtPagesAtMip(pagesX0, mip) + x;
    }

    // Inverse of vtPageLinearIndex: decode a within-block entry index to (mip, x, y).
    // Returns false if idx is out of range for the pyramid (needed to decode feedback bits).
    inline bool vtDecodeEntry(uint32_t pagesX0, uint32_t pagesY0, uint32_t mipCount,
                              uint32_t idx, uint32_t& mip, uint32_t& x, uint32_t& y)
    {
        uint32_t off = 0;
        for (uint32_t m = 0; m < mipCount; ++m)
        {
            const uint32_t px = vtPagesAtMip(pagesX0, m);
            const uint32_t py = vtPagesAtMip(pagesY0, m);
            const uint32_t count = px * py;
            if (idx < off + count)
            {
                const uint32_t local = idx - off;
                mip = m;
                x = local % px;
                y = local / px;
                return true;
            }
            off += count;
        }
        return false;
    }

    // --- Physical pool sizing -----------------------------------------------
    inline uint32_t vtTilesPerSide(uint32_t poolDim) { return poolDim / VT_PAGE_SIZE; }

    inline uint32_t vtMaxTiles(uint32_t poolDim)
    {
        const uint32_t s = poolDim / VT_PAGE_SIZE;
        return s * s;
    }

    // Hardware-safe upper bound on the atlas edge (maxImageDimension2D is 16384 on essentially all
    // desktop GPUs). A large SVT budget would otherwise compute an edge that exceeds it.
    inline constexpr uint32_t VT_MAX_POOL_DIM = 16384;

    // Largest atlas edge (multiple of VT_PAGE_SIZE, <= VT_MAX_POOL_DIM) whose `planes` images of
    // `bytesPerTexel` fit within budgetMB. planeCount/bytesPerTexel model an MRT pool
    // (RVT = 2 planes x 4 B; SVT = 1 plane BC7 ~1 B). Always at least one page.
    inline uint32_t vtPoolDimForBudget(uint32_t budgetMB, uint32_t planes, uint32_t bytesPerTexel)
    {
        if (planes == 0u) planes = 1u;
        if (bytesPerTexel == 0u) bytesPerTexel = 1u;
        const uint64_t budgetBytes = static_cast<uint64_t>(budgetMB) << 20;
        const uint64_t maxTexels = budgetBytes / (static_cast<uint64_t>(planes) * bytesPerTexel);
        uint32_t dim = static_cast<uint32_t>(std::floor(std::sqrt(static_cast<double>(maxTexels))));
        dim = (dim / VT_PAGE_SIZE) * VT_PAGE_SIZE;
        if (dim < VT_PAGE_SIZE) dim = VT_PAGE_SIZE;
        if (dim > VT_MAX_POOL_DIM) dim = VT_MAX_POOL_DIM;
        return dim;
    }

    // --- Virtual image descriptor (CPU-side) --------------------------------
    struct VTImageDesc
    {
        uint32_t pagesX0 = 0;       // pages across at mip 0
        uint32_t pagesY0 = 0;       // pages down at mip 0
        uint32_t mipCount = 0;      // vtComputeMipCount(pagesX0, pagesY0)
        uint32_t pageTableBase = 0; // offset of this image's block in the global page table

        void computeMips() { mipCount = vtComputeMipCount(pagesX0, pagesY0); }
        [[nodiscard]] uint32_t blockEntryCount() const { return vtBlockEntryCount(pagesX0, pagesY0, mipCount); }
    };

    // GPU-upload mirror of the GLSL `VTImageInfo` struct (resources/shaders/common/vt_types.glsl):
    // 8 uints = 32 bytes, std140/std430-friendly. Lets a shader resolve entirely from this struct
    // (push constant for RVT's single image, or an SSBO array element for SVT's many images).
    struct alignas(16) GPUVTImageInfo
    {
        uint32_t pagesX0 = 0;
        uint32_t pagesY0 = 0;
        uint32_t mipCount = 0;
        uint32_t pageTableBase = 0;
        uint32_t poolDim = 0;
        uint32_t pad0 = 0;
        uint32_t pad1 = 0;
        uint32_t pad2 = 0;
    };
    static_assert(sizeof(GPUVTImageInfo) == 32, "GPUVTImageInfo must match GLSL VTImageInfo (8 uints)");

    // Stable 64-bit key for a page across all virtual images (imageId | mip | x | y).
    // 24 bits image, 4 bits mip, 12 bits x, 12 bits y.
    inline uint64_t vtPageKey(uint32_t imageId, uint32_t mip, uint32_t x, uint32_t y)
    {
        return (static_cast<uint64_t>(imageId & 0xFFFFFFu) << 40)
             | (static_cast<uint64_t>(mip & 0xFu) << 36)
             | (static_cast<uint64_t>(x & 0xFFFu) << 24)
             | (static_cast<uint64_t>(y & 0xFFFu) << 12);
    }
}
