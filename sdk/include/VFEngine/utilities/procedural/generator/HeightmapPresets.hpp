#pragma once

#include "../ProceduralExport.hpp"
#include "HeightmapParams.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace procedural
{
    // VK-1653: the Heightmap Generator window's presets as data, shared by the window and the MCP
    // terrain_generate_heightmap tool (editor/handlers/HeightmapGenerationHandler), so the two can
    // never drift. Listed in the window's combo order (after its "Custom" entry).
    enum class HeightmapPreset : uint8_t
    {
        FlatHills,
        RollingPlains,
        Mountains,
        SharpPeaks,
        DeepValleys,
        Plateaus,
        Islands
    };

    struct HeightmapPresetInfo
    {
        HeightmapPreset preset;
        const char* id;          // stable, single lowercase word: the MCP enum value -- never rename
        const char* displayName; // the generator window's combo label
    };

    // Presets are tuned at the window's default resolution, and noise frequency is measured per
    // pixel, so a preset rendered at another resolution has to be rescaled to keep its look.
    inline constexpr uint32_t heightmapPresetReferenceResolution = 4096;

    // Every preset, in the window's combo order.
    VF_PROCEDURAL_API std::span<const HeightmapPresetInfo> heightmapPresets();

    // Exact, case-sensitive id match; nullopt for an unknown id (including "custom").
    VF_PROCEDURAL_API std::optional<HeightmapPreset> findHeightmapPreset(std::string_view id);

    // Resets domain warping, inversion and terracing, then sets the noise, fractal and
    // post-processing fields the preset defines. Width, height and seed are left alone, as are the
    // warp amplitude/frequency of a preset that keeps warping off.
    VF_PROCEDURAL_API void applyHeightmapPreset(HeightmapPreset preset, HeightmapParams& params);

    // Rescales the per-pixel feature settings of `params` (tuned at
    // heightmapPresetReferenceResolution) for a `resolution`-pixel heightmap, then makes every
    // feature `featureScale` times larger:
    //   resolutionIndependent:  frequency, warp frequency *= reference / resolution;
    //                           warp amplitude *= resolution / reference
    //   featureScale:           frequency, warp frequency /= featureScale;
    //                           warp amplitude *= featureScale
    // Warp amplitude is a displacement in pixels, hence it scales the other way. Width and height
    // are not touched. A zero resolution or a featureScale <= 0 skips its step.
    VF_PROCEDURAL_API void scaleHeightmapFeatures(HeightmapParams& params, uint32_t resolution,
                                                  bool resolutionIndependent, float featureScale);
}
