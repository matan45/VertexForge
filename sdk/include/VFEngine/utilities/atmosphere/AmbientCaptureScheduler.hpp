#pragma once
#include <cstdint>

// VK-1569 — Time-slicing scheduler for dynamic sky -> IBL ambient capture.
//
// Pure, allocation-free, Vulkan-free state machine (fully doctestable on the CPU, same as
// SunTransmittance.hpp / SunEntitySync.hpp). It sequences one capture "cycle" as a flat list of
// work items — env(6) -> irradiance(6) -> prefilter(6 faces x 5 mips = 30) = 42 total — and hands
// out at most `budget` of them per frame so the GPU cost is spread across ~totalItems/budget frames
// (default budget 6 => ~7 frames per full refresh). The completed cycle is published to the live
// cubemaps exactly once; a change in the sky state (sun direction / time-of-day / weather) restarts
// the cycle and a partial cycle is NEVER published.
namespace render::atmosphere
{
    enum class CapturePhase : uint8_t
    {
        Env,        // render the atmosphere sky into a scratch env cubemap face
        Irradiance, // cosine-convolve the env cube into a staging irradiance face
        Prefilter   // GGX-prefilter the env cube into a staging prefilter (face, mip)
    };

    struct AmbientCapturePlan
    {
        uint32_t envFaces = 6;
        uint32_t irrFaces = 6;
        uint32_t prefilterFaces = 6;
        // 128^2 prefilter with a full-ish chain of 5 mips: roughness = m/(mipLevels-1) = m/4.
        // Consumers sample textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD) with
        // MAX_REFLECTION_LOD == 4.0, so only LOD 0..4 are ever read — 5 mips is exact and complete.
        uint32_t prefilterMips = 5;

        [[nodiscard]] constexpr uint32_t prefilterItems() const { return prefilterFaces * prefilterMips; } // 30
        [[nodiscard]] constexpr uint32_t totalItems() const { return envFaces + irrFaces + prefilterItems(); } // 42
    };

    struct WorkItem
    {
        CapturePhase phase;
        uint32_t face; // cubemap face 0..5
        uint32_t mip;  // meaningful only for CapturePhase::Prefilter (0 otherwise)
    };

    struct AmbientCaptureScheduler
    {
        AmbientCapturePlan plan{};
        uint32_t cursor = 0;    // work items completed so far in the current cycle
        bool published = false; // has the (completed) cycle been copied to the live maps yet?
        uint64_t epoch = 0;     // invalidation key of the sky state this cycle is capturing

        [[nodiscard]] constexpr uint32_t totalItems() const { return plan.totalItems(); }
        [[nodiscard]] constexpr bool cycleComplete() const { return cursor >= plan.totalItems(); }
        [[nodiscard]] constexpr uint32_t remaining() const
        {
            return cursor >= plan.totalItems() ? 0u : plan.totalItems() - cursor;
        }

        // Map a linear index [0, totalItems) to a concrete work item.
        // Layout: Env faces 0..5, then Irradiance faces 0..5, then Prefilter MIP-MAJOR
        // (mip 0 faces 0..5, mip 1 faces 0..5, ...) to match PrefilteredEnvGenerator's for(m){for(face)}.
        [[nodiscard]] constexpr WorkItem itemAt(uint32_t i) const
        {
            const uint32_t envEnd = plan.envFaces;
            const uint32_t irrEnd = envEnd + plan.irrFaces;
            if (i < envEnd)
                return WorkItem{CapturePhase::Env, i, 0};
            if (i < irrEnd)
                return WorkItem{CapturePhase::Irradiance, i - envEnd, 0};
            const uint32_t p = i - irrEnd;                 // 0 .. prefilterItems-1
            const uint32_t face = p % plan.prefilterFaces; // mip-major ordering
            const uint32_t mip = p / plan.prefilterFaces;
            return WorkItem{CapturePhase::Prefilter, face, mip};
        }

        // Emit up to `budget` work items from the current cursor, advancing it. Returns the count
        // emitted (0 once the cycle is complete). `emit` is invoked as emit(const WorkItem&). A budget
        // larger than the remaining work emits only the remainder — the cursor never overruns.
        template <class Emit>
        uint32_t advance(uint32_t budget, Emit&& emit)
        {
            const uint32_t rem = remaining();
            const uint32_t n = budget < rem ? budget : rem;
            for (uint32_t k = 0; k < n; ++k)
                emit(itemAt(cursor + k));
            cursor += n;
            return n;
        }

        // True exactly once per completed cycle, until markPublished() is called.
        [[nodiscard]] constexpr bool shouldPublish() const { return cycleComplete() && !published; }
        void markPublished() { published = true; }

        // Begin a fresh cycle for sky-state `newEpoch`, discarding any partial progress. A partial
        // cycle is dropped without publishing (shouldPublish() stays false until the new cycle finishes).
        void restart(uint64_t newEpoch)
        {
            cursor = 0;
            published = false;
            epoch = newEpoch;
        }

        // Whether the live sky state `currentEpoch` differs from the one this cycle is capturing.
        [[nodiscard]] constexpr bool needsRestart(uint64_t currentEpoch) const { return currentEpoch != epoch; }
    };
}
