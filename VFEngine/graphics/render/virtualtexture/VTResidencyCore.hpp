#pragma once

#include "VTTypes.hpp"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

// ============================================================================
// Virtual Texturing (VK-1209) — page residency policy. PURE / header-only so the
// riskiest logic (what to stream in, what to evict) is fully CPU unit-tested with
// no Vulkan device (test_vt_residency). Copy-adapts VSM's streaming policy:
// LRU-by-last-used eviction heap (ShadowSystem.cpp:355-418) + a per-frame page
// budget (MAX_NEW_PAGES_PER_FRAME, ShadowSystem.hpp:41) + an age threshold before
// a page becomes evictable (avoids thrashing a page that reappears next frame).
//
// Contract: planFrame() decides; the Vulkan shell executes (alloc tiles, upload,
// map/unmap page-table entries) and then calls commitAllocation/commitEviction.
// A page requested this frame is never evicted; pinned pages (e.g. the coarsest
// RVT mip) are never evicted.
// ============================================================================

namespace render::vt
{
    struct VTPageKey
    {
        uint32_t imageId = 0;
        uint32_t mip = 0;
        uint32_t x = 0;
        uint32_t y = 0;

        [[nodiscard]] uint64_t packed() const { return vtPageKey(imageId, mip, x, y); }
        bool operator==(const VTPageKey& o) const
        {
            return imageId == o.imageId && mip == o.mip && x == o.x && y == o.y;
        }
    };

    struct VTResidentPage
    {
        VTPageKey key;
        uint32_t tile = VT_INVALID_TILE;
        uint32_t lastUsedFrame = 0;
        bool pinned = false;
    };

    struct VTResidencyPlan
    {
        std::vector<VTPageKey> toAllocate; // requested, not resident (bounded by pagesPerFrame + evictable room)
        std::vector<VTPageKey> toEvict;    // freed to make room this frame
    };

    class VTResidencyCore
    {
    public:
        void clear() { resident.clear(); }

        [[nodiscard]] bool isResident(const VTPageKey& k) const { return resident.count(k.packed()) != 0; }
        [[nodiscard]] uint32_t residentCount() const { return static_cast<uint32_t>(resident.size()); }

        // VK-1539 read-only per-owner walk: invoke fn(imageId) for every resident page so the SVT
        // manager can tally page counts grouped by owning image (× tileByteSize → per-asset VRAM)
        // without exposing the private residency map. Non-mutating sibling of evictImage().
        template <typename Fn>
        void forEachResident(Fn&& fn) const
        {
            for (const auto& entry : resident)
                fn(entry.second.key.imageId);
        }

        [[nodiscard]] uint32_t tileFor(const VTPageKey& k) const
        {
            auto it = resident.find(k.packed());
            return it == resident.end() ? VT_INVALID_TILE : it->second.tile;
        }

        void touch(const VTPageKey& k, uint32_t frame)
        {
            auto it = resident.find(k.packed());
            if (it != resident.end())
                it->second.lastUsedFrame = frame;
        }

        void commitAllocation(const VTPageKey& k, uint32_t tile, uint32_t frame, bool pinned)
        {
            VTResidentPage p;
            p.key = k;
            p.tile = tile;
            p.lastUsedFrame = frame;
            p.pinned = pinned;
            resident[k.packed()] = p;
        }

        // Returns the freed tile (VT_INVALID_TILE if the page was not resident).
        uint32_t commitEviction(const VTPageKey& k)
        {
            auto it = resident.find(k.packed());
            if (it == resident.end())
                return VT_INVALID_TILE;
            const uint32_t tile = it->second.tile;
            resident.erase(it);
            return tile;
        }

        // VK-1209 per-texture reclaim: remove every resident page owned by imageId, invoking
        // fn(key, tile) for each so the shell frees the physical tile + unmaps its page-table entry.
        // Iterates only the resident set (cheap), not the image's full pyramid. Returns the count.
        template <typename Fn>
        uint32_t evictImage(uint32_t imageId, Fn&& fn)
        {
            uint32_t removed = 0;
            for (auto it = resident.begin(); it != resident.end();)
            {
                if (it->second.key.imageId == imageId)
                {
                    fn(it->second.key, it->second.tile);
                    it = resident.erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
            return removed;
        }

        // Decide the frame's streaming work. `requested` should be pre-sorted by priority
        // (near / finer mip first): under budget pressure the earliest entries win.
        VTResidencyPlan planFrame(const std::vector<VTPageKey>& requested,
                                  uint32_t frame,
                                  uint32_t freeTiles,
                                  uint32_t pagesPerFrame,
                                  uint32_t evictionAgeFrames)
        {
            VTResidencyPlan plan;

            std::vector<VTPageKey> wanted;
            std::unordered_set<uint64_t> wantedSeen;
            std::unordered_set<uint64_t> requestedThisFrame;
            requestedThisFrame.reserve(requested.size() * 2u);

            // 1. Touch resident requests (protects them this frame); collect deduped misses in order.
            for (const auto& k : requested)
            {
                const uint64_t pk = k.packed();
                requestedThisFrame.insert(pk);
                auto it = resident.find(pk);
                if (it != resident.end())
                    it->second.lastUsedFrame = frame;
                else if (wantedSeen.insert(pk).second)
                    wanted.push_back(k);
            }

            if (wanted.empty())
                return plan;

            // 2. Cap new allocations this frame.
            uint32_t canAlloc = std::min<uint32_t>(pagesPerFrame, static_cast<uint32_t>(wanted.size()));

            // 3. Evict LRU aged-out pages if free tiles can't cover the allocations.
            if (freeTiles < canAlloc)
            {
                const uint32_t deficit = canAlloc - freeTiles;

                std::vector<const VTResidentPage*> cands;
                cands.reserve(resident.size());
                for (const auto& entry : resident)
                {
                    const VTResidentPage& page = entry.second;
                    if (page.pinned)
                        continue;
                    if (requestedThisFrame.count(entry.first))
                        continue;
                    if (frame < page.lastUsedFrame)
                        continue; // guard against unsigned wrap
                    if ((frame - page.lastUsedFrame) <= evictionAgeFrames)
                        continue; // not aged out yet
                    cands.push_back(&page);
                }
                std::sort(cands.begin(), cands.end(),
                          [](const VTResidentPage* a, const VTResidentPage* b)
                          { return a->lastUsedFrame < b->lastUsedFrame; });

                const uint32_t evictCount = std::min<uint32_t>(deficit, static_cast<uint32_t>(cands.size()));
                plan.toEvict.reserve(evictCount);
                for (uint32_t i = 0; i < evictCount; ++i)
                    plan.toEvict.push_back(cands[i]->key);

                const uint32_t available = freeTiles + evictCount;
                if (canAlloc > available)
                    canAlloc = available;
            }

            plan.toAllocate.assign(wanted.begin(), wanted.begin() + canAlloc);
            return plan;
        }

    private:
        std::unordered_map<uint64_t, VTResidentPage> resident;
    };
}
