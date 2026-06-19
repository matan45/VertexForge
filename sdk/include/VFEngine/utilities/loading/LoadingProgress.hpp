#pragma once

#include <algorithm>

// Aggregate loading-progress model (VK-1268). Pure, header-only, CPU-testable.
//
// A loading screen needs a single 0-1 bar and a "what's happening now" label,
// but the real work is spread across several engine subsystems that each track
// their own progress. This combines those per-subsystem fractions into one
// weighted value using the phase bands the ticket specifies:
//
//   terrain  -> 0-40%   terrain tile generation / GPU upload
//   sectors  -> 40-70%  sector entity loading
//   gpu      -> 70-90%  GPU resource streaming / texture uploads
//   init     -> 90-100% final init: physics, navmesh, scripts
namespace loading
{
    // Per-phase completion fractions in [0,1]. 1.0 means that phase is done
    // (or has no work). Defaults are all-complete so partially-populated inputs
    // read as "that subsystem isn't gating the load".
    struct LoadingPhaseInputs
    {
        float terrain = 1.0f;
        float sectors = 1.0f;
        float gpu = 1.0f;
        float init = 1.0f;
    };

    enum class LoadingPhase
    {
        Idle,          // nothing loading
        Terrain,
        Sectors,
        GpuStreaming,
        Initializing,
        Complete       // a load is active but every phase finished
    };

    // Band weights (sum to 1.0), matching the VK-1268 phase split.
    inline constexpr float kTerrainWeight = 0.40f;
    inline constexpr float kSectorsWeight = 0.30f;
    inline constexpr float kGpuWeight = 0.20f;
    inline constexpr float kInitWeight = 0.10f;

    struct LoadingProgress
    {
        float fraction = 1.0f;                 // overall 0-1
        LoadingPhase phase = LoadingPhase::Idle;
    };

    // Human-readable status text, matching the ticket's example strings.
    [[nodiscard]] inline const char* loadingPhaseLabel(LoadingPhase phase)
    {
        switch (phase)
        {
            case LoadingPhase::Idle:         return "Idle";
            case LoadingPhase::Terrain:      return "Generating terrain...";
            case LoadingPhase::Sectors:      return "Loading sectors...";
            case LoadingPhase::GpuStreaming: return "Streaming resources...";
            case LoadingPhase::Initializing: return "Initializing...";
            case LoadingPhase::Complete:     return "Ready";
        }
        return "";
    }

    // Combine per-phase fractions into one weighted progress + the current phase.
    // `active` separates "a load is running" (all-done -> Complete) from "idle"
    // (nothing was requested -> Idle). The current phase is the earliest band
    // that has not yet finished, so the status text advances through the load.
    [[nodiscard]] inline LoadingProgress computeLoadingProgress(const LoadingPhaseInputs& in,
                                                                bool active = true)
    {
        const auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
        const float terrain = clamp01(in.terrain);
        const float sectors = clamp01(in.sectors);
        const float gpu = clamp01(in.gpu);
        const float init = clamp01(in.init);

        LoadingProgress out;
        out.fraction = kTerrainWeight * terrain + kSectorsWeight * sectors +
                       kGpuWeight * gpu + kInitWeight * init;

        if (!active)
        {
            out.phase = LoadingPhase::Idle;
            return out;
        }

        constexpr float kDone = 0.9999f;
        if (terrain < kDone)      out.phase = LoadingPhase::Terrain;
        else if (sectors < kDone) out.phase = LoadingPhase::Sectors;
        else if (gpu < kDone)     out.phase = LoadingPhase::GpuStreaming;
        else if (init < kDone)    out.phase = LoadingPhase::Initializing;
        else                      out.phase = LoadingPhase::Complete;

        return out;
    }
} // namespace loading
