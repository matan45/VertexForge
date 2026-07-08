#pragma once

#include "VTTypes.hpp"
#include <cstdint>
#include <vector>
#include <algorithm>

// ============================================================================
// Virtual Texturing (VK-1209, Phase 2 SVT) — extract one physical-page-sized tile
// (VT_PAGE_SIZE texels) from a source texture mip, for streamed material textures.
// PURE / header-only so the block-alignment + edge-clamp logic is unit-tested
// (test_vt_page_extraction) without any Vulkan device or file I/O.
//
// A page's usable content is VT_PAGE_INTERIOR texels; it is surrounded by a
// VT_BORDER-texel margin taken from neighbouring texels so hardware filtering near
// a page edge stays correct. For block-compressed formats (BC7: 4x4 texels, 16 B),
// VT_PAGE_SIZE/VT_PAGE_INTERIOR/VT_BORDER are all multiples of 4, so a page maps to
// a whole number of blocks and the copy is block-exact. Out-of-range blocks clamp
// to the edge block (replicate).
// ============================================================================

namespace render::vt
{
    struct VTBlockFormat
    {
        uint32_t blockTexels = 1; // BC7 = 4, uncompressed = 1
        uint32_t blockBytes = 4;  // BC7 = 16, RGBA8 = 4
    };

    inline constexpr VTBlockFormat VT_FORMAT_BC7{4, 16};
    inline constexpr VTBlockFormat VT_FORMAT_RGBA8{1, 4};

    // Blocks along one axis of `mipTexels` texels for `fmt`.
    inline uint32_t vtBlocksPerAxis(uint32_t mipTexels, const VTBlockFormat& fmt)
    {
        return (mipTexels + fmt.blockTexels - 1u) / fmt.blockTexels;
    }

    // Bytes a full VT_PAGE_SIZE tile occupies for `fmt`.
    inline uint32_t vtTileByteSize(const VTBlockFormat& fmt)
    {
        const uint32_t tileBlocks = VT_PAGE_SIZE / fmt.blockTexels;
        return tileBlocks * tileBlocks * fmt.blockBytes;
    }

    // Extract tile (pageX, pageY) at a mip of mipWidth x mipHeight texels from `src`
    // (row-major blocks) into `out` (resized to vtTileByteSize). The tile's top-left texel
    // is pageX*VT_PAGE_INTERIOR - VT_BORDER; edge blocks clamp/replicate. Returns false on
    // bad input (src too small, degenerate format).
    inline bool vtExtractTile(const uint8_t* src, uint32_t srcSize,
                              uint32_t mipWidth, uint32_t mipHeight,
                              const VTBlockFormat& fmt,
                              uint32_t pageX, uint32_t pageY,
                              std::vector<uint8_t>& out)
    {
        if (src == nullptr || fmt.blockTexels == 0u || fmt.blockBytes == 0u)
            return false;

        const uint32_t mipBlocksX = vtBlocksPerAxis(mipWidth, fmt);
        const uint32_t mipBlocksY = vtBlocksPerAxis(mipHeight, fmt);
        if (mipBlocksX == 0u || mipBlocksY == 0u)
            return false;
        if (srcSize < mipBlocksX * mipBlocksY * fmt.blockBytes)
            return false;

        const uint32_t tileBlocks = VT_PAGE_SIZE / fmt.blockTexels;      // e.g. BC7 128/4 = 32
        const int32_t startTexelX = static_cast<int32_t>(pageX * VT_PAGE_INTERIOR) - static_cast<int32_t>(VT_BORDER);
        const int32_t startTexelY = static_cast<int32_t>(pageY * VT_PAGE_INTERIOR) - static_cast<int32_t>(VT_BORDER);
        const int32_t startBlockX = startTexelX / static_cast<int32_t>(fmt.blockTexels);
        const int32_t startBlockY = startTexelY / static_cast<int32_t>(fmt.blockTexels);

        out.assign(vtTileByteSize(fmt), 0u);

        for (uint32_t by = 0; by < tileBlocks; ++by)
        {
            int32_t sby = startBlockY + static_cast<int32_t>(by);
            sby = std::clamp(sby, 0, static_cast<int32_t>(mipBlocksY) - 1);
            for (uint32_t bx = 0; bx < tileBlocks; ++bx)
            {
                int32_t sbx = startBlockX + static_cast<int32_t>(bx);
                sbx = std::clamp(sbx, 0, static_cast<int32_t>(mipBlocksX) - 1);

                const uint32_t srcOff = (static_cast<uint32_t>(sby) * mipBlocksX + static_cast<uint32_t>(sbx)) * fmt.blockBytes;
                const uint32_t dstOff = (by * tileBlocks + bx) * fmt.blockBytes;
                std::copy_n(src + srcOff, fmt.blockBytes, out.begin() + dstOff);
            }
        }
        return true;
    }
}
