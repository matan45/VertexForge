#pragma once

#include <cstdint>

// ============================================================================
// Virtual Texturing (VK-1480) — tail-only fallback mip selection. PURE /
// header-only so the Tests project validates the S rule directly
// (test_vt_svt_async / addressing tests) without a Vulkan device.
//
// An SVT-registered texture no longer keeps its full-pyramid whole-image copy
// resident as the streaming fallback (the source of the "SVT adds VRAM without
// saving any" bug). Instead the streamer keeps a tiny TAIL-ONLY image starting
// at mip S — the first source mip whose longer edge is <= 128 px — which is a
// permanently-valid, sub-pixel fallback while a page streams in. This picks S.
// ============================================================================

namespace render::gpudriven
{
    // Source mip index a tail-only fallback image starts at: the first mip S with
    // max(width>>S, height>>S) <= 128. If the chain never gets that small (a
    // truncated source chain), returns the coarsest stored level (mipLevels-1) so
    // the fallback is always the smallest available level. mipLevels==0 -> 0.
    inline uint32_t svtTailStartMip(uint32_t width, uint32_t height, uint32_t mipLevels)
    {
        if (mipLevels == 0u)
            return 0u;
        for (uint32_t s = 0; s < mipLevels; ++s)
        {
            uint32_t w = width >> s;
            uint32_t h = height >> s;
            if (w == 0u) w = 1u;
            if (h == 0u) h = 1u;
            if ((w > h ? w : h) <= 128u)
                return s;
        }
        return mipLevels - 1u;
    }
}
