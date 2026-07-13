#pragma once

// VK-1503 (VFX M4 slice-c) — VFX significance cap.
//
// Pure, header-only decision core for bounding the number of LIVE VFX effect
// instances by importance. Each tick the renderer scores every live instance by
// `significance / max(distanceSq, eps)`, keeps the top-N against a scene-wide
// budget, and soft-stops (ceases emission, lets existing particles fade — no hard
// kill) the least-significant fire-and-forget one-shots. Looping / attached /
// ambient effects are structurally protected. A rank-based hysteresis dead-band
// prevents flapping at the budget boundary.
//
// glm-free / Vulkan-free (like VFXScalability.hpp / VFXBoundsUtil.hpp) so the
// Services-only Tests project can unit-test the scorer, the deterministic top-N
// selection, and the hysteresis directly, with no rendering dependencies.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "VFXScalability.hpp" // VFXQualityTier / kVFXQualityTierCount

namespace vfx
{
    // Guards the score against an emitter sitting on the camera (distanceSq -> 0).
    inline constexpr float kSignificanceEpsilon = 1e-4f; // world-units^2

    // Default re-admission dead-band width k (see nextSuppressed): an instance is
    // evicted at rank >= N and only re-admitted once it climbs to rank < N - k, so a
    // one whose distance jitters around the boundary does not stop/start/stop.
    inline constexpr int kSignificanceHysteresis = 4;

    // Advisory per-tier live-instance budgets. Shipped as 0 (== unlimited) so the cap
    // is strictly opt-in and existing scenes stay byte-identical (VK-1453 discipline);
    // the renderer's active budget is a separate scalar knob. A caller that wants
    // tier-driven caps can copy tuned values from here — suggested tuned set:
    // { 128, 256, 512, 1024 } for Low / Medium / High / Ultra.
    inline constexpr uint32_t kDefaultMaxLiveInstances[kVFXQualityTierCount] = {0, 0, 0, 0};

    inline uint32_t defaultMaxLiveInstances(VFXQualityTier tier)
    {
        const int idx = std::clamp(static_cast<int>(tier), 0, kVFXQualityTierCount - 1);
        return kDefaultMaxLiveInstances[idx];
    }

    // Importance of a live instance: higher = keep. Closer effects and higher-
    // significance effects score higher. Always finite (eps floor on the denominator).
    inline float significanceScore(float significance, float distanceSq)
    {
        return significance / std::max(distanceSq, kSignificanceEpsilon);
    }

    // One live instance considered by the cap. `id` is the immutable instance handle,
    // used as the final tie-break so selection never depends on unordered_map
    // iteration order. `evictable` is false for structurally-protected effects
    // (loops / attached / camera-relative / channel listeners / Critical priority).
    struct VFXSignificanceCandidate
    {
        uint32_t id = 0;
        float    score = 0.0f;
        bool     evictable = false;
        bool     currentlyEvicted = false; // this instance's hysteresis state, fed back each tick
    };

    // Strict-weak "more significant than" ordering: protected outrank evictable, then
    // by score descending, then by id ascending (older / lower id survives ties).
    // Because id is unique this is a total order, so the kept top-N set is fully
    // deterministic regardless of the caller's gather order.
    inline bool moreSignificant(const VFXSignificanceCandidate& a, const VFXSignificanceCandidate& b)
    {
        if (a.evictable != b.evictable)
            return !a.evictable; // protected (evictable == false) ranks first
        if (a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    }

    // Hysteresis transition for one instance at `rank` (0-based, over the full
    // eligible population) against budget `budgetN` with dead-band width `k`:
    //   rank >= budgetN      -> suppress
    //   rank <  budgetN - k  -> admit
    //   otherwise            -> hold previous state (the [N-k, N) dead-band)
    inline bool nextSuppressed(bool prev, int rank, int budgetN, int k)
    {
        if (rank >= budgetN)
            return true;
        if (rank < budgetN - k)
            return false;
        return prev;
    }

    // Deterministically decide which candidates are soft-stopped this tick.
    // `outEvicted` is resized to cands.size(); outEvicted[i] corresponds to cands[i]
    // (1 = soft-stopped this frame, 0 = live). budgetN <= 0 disables the cap (nothing
    // evicted). Protected candidates are never evicted, but they are ranked first and
    // so consume budget — evictable one-shots are ranked against the remaining
    // headroom (evictableBudget = budgetN - protectedCount). Ranking includes already-
    // suppressed instances so suppression never changes anyone's rank (no feedback
    // loop => provably no oscillation).
    inline void selectSignificanceEvictions(const std::vector<VFXSignificanceCandidate>& cands,
                                            int budgetN, int k, std::vector<char>& outEvicted)
    {
        outEvicted.assign(cands.size(), 0);
        if (budgetN <= 0 || cands.empty())
            return;

        std::vector<uint32_t> order(cands.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(order.size()); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&cands](uint32_t l, uint32_t r) {
            return moreSignificant(cands[l], cands[r]);
        });

        for (int rank = 0; rank < static_cast<int>(order.size()); ++rank)
        {
            const uint32_t idx = order[static_cast<size_t>(rank)];
            const VFXSignificanceCandidate& c = cands[idx];
            if (!c.evictable)
            {
                outEvicted[idx] = 0; // protected: always live (may exceed budget)
                continue;
            }
            outEvicted[idx] = nextSuppressed(c.currentlyEvicted, rank, budgetN, k) ? 1 : 0;
        }
    }
}
