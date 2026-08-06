#pragma once
#include <cstdint>
#include <mutex>

namespace render
{
    // VK-1610 — terrain Runtime Virtual Texture residency, published by the graphics module
    // (GPUDrivenRenderer drives the residency update) and read by the editor profiler UI. Same
    // sink pattern as GpuPassStats / FrameDrawStats: utilities owns the type so editor windows
    // never include graphics headers.
    //
    // This exists because the RVT's page pool has a failure mode with no other symptom. Detail
    // maps take the atlas from 2 planes to 4, which at a fixed MB budget cuts resident pages from
    // 1024 to 400 — and a pool that cannot hold the camera's footprint silently evicts and
    // re-bakes pages every frame. That shows up as GPU time in the bake scope and as nothing at
    // all anywhere else, so "is it thrashing?" was previously unanswerable.
    //
    // NOT FOR PLUGIN USE. `premake5 vs2022` mirrors utilities headers into sdk/, but the sink is a
    // header-only function-local static: a plugin DLL that included it would get its own private
    // instance and read an empty struct forever. Only engine code linked into the same image
    // (Graphics, Editor) may use it. Same constraint as GpuPassStats.
    struct TerrainRVTFrameStats
    {
        bool active = false; // an initialized RVT manager exists for the current terrain

        // Pool geometry actually built (not what the setting asked for).
        uint32_t poolDim = 0;
        uint32_t planeCount = 0;
        uint32_t bytesPerTexel = 0;
        uint32_t budgetMB = 0;
        uint32_t capacityPages = 0;
        uint32_t residentPages = 0;

        // Per-frame residency traffic.
        uint32_t requestedPages = 0;
        uint32_t allocatedPages = 0;
        uint32_t evictedPages = 0;
        uint32_t scheduledBakes = 0;
        uint32_t uncoveredSkipped = 0;
        // Wanted this frame but left non-resident, and which bound caused it. `poolLimited` is real
        // thrash - the atlas cannot hold the camera's footprint, so pages evict and re-bake every
        // frame. `budgetLimited` is benign: the pool has room and is simply pacing how much it
        // bakes per frame. Exactly one can be true.
        uint32_t unmetPages = 0;
        bool budgetLimited = false;
        bool poolLimited = false;

        // Render-thread CPU cost of the residency path.
        uint64_t readbackUs = 0;
        uint64_t decodeUs = 0;
        uint64_t residencyUs = 0;
    };

    class TerrainRVTStats
    {
    public:
        static TerrainRVTStats& instance()
        {
            static TerrainRVTStats inst;
            return inst;
        }

        // Called once per frame from the render thread. Cheap enough to leave unconditional: the
        // numbers are already computed by the residency update, and the only reader is a profiler
        // window polling a snapshot, so the lock is uncontended in practice.
        void publish(const TerrainRVTFrameStats& stats)
        {
            std::lock_guard lock(mutex);
            latest = stats;
        }

        [[nodiscard]] TerrainRVTFrameStats snapshot() const
        {
            std::lock_guard lock(mutex);
            return latest;
        }

        // Terrain unloaded / RVT disabled: publish an inactive frame rather than leaving the last
        // live numbers on screen, where they would read as current.
        void clear()
        {
            std::lock_guard lock(mutex);
            latest = {};
        }

    private:
        TerrainRVTStats() = default;

        mutable std::mutex mutex;
        TerrainRVTFrameStats latest;
    };
}
