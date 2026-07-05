#pragma once

#include "VTTypes.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>

// ============================================================================
// Virtual Texturing (VK-1209) — the virtual->physical UV translation, written as
// a PURE C++ mirror of resources/shaders/common/vt_sampling.glsl so parity is
// guaranteed by a CPU unit test (test_vt_addressing), the same "parity by
// construction" approach used for VFX mesh orientation (VK-1476) and the VSM
// page-overlap projection.
//
// Sampling walks the page table from the desired (finest) mip toward coarser mips
// until a resident page is found, then maps the logical page UV into that tile's
// bordered interior. Because the coarsest mip is pinned resident, a lookup ALWAYS
// resolves — a page still streaming in shows briefly blurry, never as a hole
// (AC6). Border inset keeps bilinear/aniso taps inside the tile.
// ============================================================================

namespace render::vt
{
    struct VTPhysicalSample
    {
        float u = 0.0f;             // physical atlas UV [0,1) across the pool image
        float v = 0.0f;
        uint32_t residentMip = 0;   // mip actually found (>= desired)
        bool valid = false;
    };

    class VTAddressing
    {
    public:
        // Logical UV [0,1) within the virtual image + desired mip -> physical atlas UV.
        // pageTable is the whole global table; img.pageTableBase locates this image's block.
        // poolDim is the physical atlas edge in texels.
        static VTPhysicalSample lookup(const VTImageDesc& img,
                                       const uint32_t* pageTable,
                                       uint32_t poolDim,
                                       float u, float v,
                                       uint32_t desiredMip)
        {
            VTPhysicalSample s;
            if (img.mipCount == 0u || pageTable == nullptr || poolDim < VT_PAGE_SIZE)
                return s;

            const float uu = std::clamp(u, 0.0f, 0.999999f);
            const float vv = std::clamp(v, 0.0f, 0.999999f);
            uint32_t mip = desiredMip < img.mipCount ? desiredMip : (img.mipCount - 1u);

            for (; mip < img.mipCount; ++mip)
            {
                const uint32_t pagesX = vtPagesAtMip(img.pagesX0, mip);
                const uint32_t pagesY = vtPagesAtMip(img.pagesY0, mip);

                uint32_t pageX = static_cast<uint32_t>(uu * static_cast<float>(pagesX));
                uint32_t pageY = static_cast<uint32_t>(vv * static_cast<float>(pagesY));
                if (pageX >= pagesX) pageX = pagesX - 1u;
                if (pageY >= pagesY) pageY = pagesY - 1u;

                const uint32_t entryIdx = img.pageTableBase
                    + vtMipSubOffset(img.pagesX0, img.pagesY0, mip)
                    + pageY * pagesX + pageX;
                const uint32_t entry = pageTable[entryIdx];
                if (!vtIsPageValid(entry))
                    continue;

                uint32_t tileX = 0, tileY = 0;
                vtUnpackPageEntry(entry, tileX, tileY);

                // Position within the page, [0,1).
                const float fx = uu * static_cast<float>(pagesX) - static_cast<float>(pageX);
                const float fy = vv * static_cast<float>(pagesY) - static_cast<float>(pageY);

                // Map into the tile's interior [BORDER, BORDER+INTERIOR) in texels, then to atlas UV.
                const float texelX = static_cast<float>(tileX * VT_PAGE_SIZE + VT_BORDER)
                                   + fx * static_cast<float>(VT_PAGE_INTERIOR);
                const float texelY = static_cast<float>(tileY * VT_PAGE_SIZE + VT_BORDER)
                                   + fy * static_cast<float>(VT_PAGE_INTERIOR);

                s.u = texelX / static_cast<float>(poolDim);
                s.v = texelY / static_cast<float>(poolDim);
                s.residentMip = mip;
                s.valid = true;
                return s;
            }
            return s;
        }

        // Desired mip from screen-space UV derivatives. virtualResTexels is the mip-0
        // texel resolution along the sampled axis (pages0 * VT_PAGE_INTERIOR). Matches
        // the standard textureQueryLod-style trilinear selector; clamped to [0, inf).
        static float desiredMipFromDerivatives(float dudx, float dvdx,
                                               float dudy, float dvdy,
                                               float virtualResTexels)
        {
            const float px = std::sqrt(dudx * dudx + dvdx * dvdx) * virtualResTexels;
            const float py = std::sqrt(dudy * dudy + dvdy * dvdy) * virtualResTexels;
            const float m = std::max(px, py);
            if (m <= 1.0f)
                return 0.0f;
            return std::log2(m);
        }
    };
}
