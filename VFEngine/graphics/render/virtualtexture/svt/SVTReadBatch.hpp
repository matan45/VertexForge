#pragma once

#include "../VTResidencyCore.hpp" // vt::VTPageKey (pure, no Vulkan)
#include <vector>
#include <algorithm>
#include <cstdint>

// ============================================================================
// Virtual Texturing (VK-1480, async SVT I/O) — pure helpers for the render-thread
// plan/drain of streamed material pages. Header-only so the Tests project
// exercises the read-plan grouping, the staging-ring offset math, and the
// drain-time re-validation table (test_vt_svt_async) with no Vulkan device and
// no disk I/O.
//
//   - svtStagingOffset : where a tile lands in the frame-rotated staging ring.
//   - svtBuildReadGroups: coalesce page requests sharing one (imageId, mip) into
//     one disk read each, within a per-frame page budget. Each group becomes one
//     worker job (readMipLevel once + vtExtractTile per page).
//   - svtDrainDecision : whether an async-produced tile is still wanted when it
//     lands on the render thread a few frames later.
// ============================================================================

namespace render::gpudriven
{
    // Byte offset of staging slot `slot` for frame-in-flight `frameSlot`. The ring
    // holds pagesPerFrame tiles per frame-in-flight; frameSlot rotates each
    // updateAndUpload so an in-flight copy's source is never overwritten (mirrors
    // VTPageTable's staging rotation, with its own counter). Pure so a test can
    // prove non-overlap across every (frameSlot, slot) pair.
    inline uint64_t svtStagingOffset(uint32_t frameSlot, uint32_t slot,
                                     uint32_t pagesPerFrame, uint32_t tileByteSize)
    {
        return (static_cast<uint64_t>(frameSlot) * pagesPerFrame + slot)
             * static_cast<uint64_t>(tileByteSize);
    }

    // A batch of page reads that share one (imageId, mip): one readMipLevel serves
    // every page in it, killing the N-pages-of-one-mip = N full re-reads pattern.
    struct SVTReadGroup
    {
        uint32_t imageId = 0;
        uint32_t mip = 0;
        std::vector<vt::VTPageKey> pages;
    };

    // Coalesce `requests` into groups keyed (imageId, mip). The TOTAL pages across
    // all returned groups is <= pageBudget (a budget in pages, not groups). Requests
    // are partitioned by (imageId, mip) via a stable sort (page order within a level
    // preserved); groups are emitted in that partition order until the budget is hit.
    inline std::vector<SVTReadGroup> svtBuildReadGroups(const std::vector<vt::VTPageKey>& requests,
                                                        uint32_t pageBudget)
    {
        std::vector<SVTReadGroup> groups;
        if (pageBudget == 0u || requests.empty())
            return groups;

        std::vector<vt::VTPageKey> sorted = requests;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const vt::VTPageKey& a, const vt::VTPageKey& b)
                         {
                             if (a.imageId != b.imageId) return a.imageId < b.imageId;
                             return a.mip < b.mip;
                         });

        uint32_t taken = 0;
        for (const auto& k : sorted)
        {
            if (taken >= pageBudget)
                break;
            if (groups.empty() || groups.back().imageId != k.imageId || groups.back().mip != k.mip)
            {
                SVTReadGroup g;
                g.imageId = k.imageId;
                g.mip = k.mip;
                groups.push_back(std::move(g));
            }
            groups.back().pages.push_back(k);
            ++taken;
        }
        return groups;
    }

    // Drain-time re-validation outcome for a tile an async job produced. Pure so the
    // decision table is unit-tested exhaustively.
    enum class SVTDrainDecision
    {
        Accept,        // still wanted — allocate a tile and upload
        DropEpoch,     // produced before an SVT reset/toggle — stale, discard
        DropImageGone, // owning image no longer exists — discard
        DropResident   // page already resident (e.g. pinned earlier) — discard
    };

    inline SVTDrainDecision svtDrainDecision(uint64_t tileEpoch, uint64_t currentEpoch,
                                             uint32_t imageId, uint32_t imageCount,
                                             bool alreadyResident)
    {
        if (tileEpoch != currentEpoch)
            return SVTDrainDecision::DropEpoch;
        if (imageId >= imageCount)
            return SVTDrainDecision::DropImageGone;
        if (alreadyResident)
            return SVTDrainDecision::DropResident;
        return SVTDrainDecision::Accept;
    }
}
