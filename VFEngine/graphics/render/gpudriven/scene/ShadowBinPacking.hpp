#pragma once

#include <cstdint>
#include <vector>

// VK-1479 Phase B1: pure CPU accounting for the page-binned shadow cull, with NO Vulkan
// dependency so the CPU-only Tests project (test_shadow_page_bin) validates the bin-slot
// assignment and the ShadowPageBinner keeps its heavy header out of the test.

namespace render::gpudriven
{
    // Per-page draw capacity and the max number of pages that can be binned in a frame. The bin
    // arenas are sized MAX_RENDERED_SHADOW_PAGES * SHADOW_BIN_CAPACITY. Ultra directional = 256
    // pages; the headroom covers B2 (spot/point) growth. A page that overflows its bin clamps and
    // increments an overflow counter (diagnosable, never silent); a page past the arena falls back
    // to the legacy loop for that page only.
    inline constexpr uint32_t SHADOW_BIN_CAPACITY = 256;
    inline constexpr uint32_t MAX_RENDERED_SHADOW_PAGES = 320;
    inline constexpr uint32_t INVALID_SHADOW_BIN_SLOT = 0xFFFFFFFFu;

    // A page that renders this frame: its light-space page-grid linear index (matches
    // ShadowSystemFeedback's (level*ppl+fy)*ppl+fx). buildPageBinBase assigns each a render slot.
    struct ShadowBinPageRequest
    {
        uint32_t pageLinear;
    };

    // Assign a contiguous render slot to each requested page (order preserved), filling
    // outPageBinBase[pageLinear] = renderSlot for rendered pages and INVALID_SHADOW_BIN_SLOT
    // elsewhere. Pages beyond MAX_RENDERED_SHADOW_PAGES are left INVALID (legacy fallback per page).
    // outPageBinBase is (re)sized to totalPageCount. Returns the number of slots assigned.
    // outAssignedSlot (optional, same length as requests) receives each request's slot (or INVALID
    // if it overflowed the arena / was an out-of-range page). Duplicate pages reuse their slot.
    inline uint32_t buildPageBinBase(const std::vector<ShadowBinPageRequest>& requests,
                                     uint32_t totalPageCount,
                                     std::vector<uint32_t>& outPageBinBase,
                                     std::vector<uint32_t>* outAssignedSlot = nullptr)
    {
        outPageBinBase.assign(totalPageCount, INVALID_SHADOW_BIN_SLOT);
        if (outAssignedSlot)
            outAssignedSlot->assign(requests.size(), INVALID_SHADOW_BIN_SLOT);

        uint32_t slot = 0;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            if (slot >= MAX_RENDERED_SHADOW_PAGES)
                break; // arena full: remaining pages stay INVALID (legacy fallback)

            const uint32_t pageLinear = requests[i].pageLinear;
            if (pageLinear >= totalPageCount)
                continue; // defensive: out-of-range page index

            if (outPageBinBase[pageLinear] != INVALID_SHADOW_BIN_SLOT)
            {
                // Duplicate page in the request list: reuse its slot, don't consume a new one.
                if (outAssignedSlot)
                    (*outAssignedSlot)[i] = outPageBinBase[pageLinear];
                continue;
            }

            outPageBinBase[pageLinear] = slot;
            if (outAssignedSlot)
                (*outAssignedSlot)[i] = slot;
            ++slot;
        }
        return slot;
    }
}
