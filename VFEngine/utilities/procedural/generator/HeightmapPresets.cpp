#include "HeightmapPresets.hpp"
#include <array>

namespace procedural
{
    namespace
    {
        // Ids are single lowercase words on purpose: MCP exposes them as an enum, and
        // test_mcp_prompts reads every snake_case token in prompt text as a tool name.
        constexpr std::array<HeightmapPresetInfo, 7> presetTable = {{
            { HeightmapPreset::FlatHills,     "hills",     "Flat Hills" },
            { HeightmapPreset::RollingPlains, "plains",    "Rolling Plains" },
            { HeightmapPreset::Mountains,     "mountains", "Mountains" },
            { HeightmapPreset::SharpPeaks,    "peaks",     "Sharp Peaks" },
            { HeightmapPreset::DeepValleys,   "valleys",   "Deep Valleys" },
            { HeightmapPreset::Plateaus,      "plateaus",  "Plateaus" },
            { HeightmapPreset::Islands,       "islands",   "Islands" },
        }};
    }

    std::span<const HeightmapPresetInfo> heightmapPresets()
    {
        return presetTable;
    }

    std::optional<HeightmapPreset> findHeightmapPreset(std::string_view id)
    {
        for (const auto& info : presetTable)
        {
            if (id == info.id)
                return info.preset;
        }
        return std::nullopt;
    }

    // Moved verbatim from HeightmapGeneratorWindow::applyPreset (VK-1653). The window set its
    // noise/fractal combo indices; here the same values go straight into params, and the window
    // copies them back into its indices.
    void applyHeightmapPreset(HeightmapPreset preset, HeightmapParams& params)
    {
        // Reset post-processing
        params.domainWarp.enabled = false;
        params.invert = false;
        params.terracing = false;
        params.terraceSteps = 8;

        switch (preset)
        {
        case HeightmapPreset::FlatHills: // Flat Hills — gentle rolling terrain
            params.noiseType = NoiseType::Simplex;
            params.fractalType = FractalType::FBM;
            params.octaves = 4;
            params.frequency = 0.002f;
            params.amplitude = 0.6f;
            params.lacunarity = 2.0f;
            params.persistence = 0.35f;
            params.heightExponent = 0.7f;
            break;

        case HeightmapPreset::RollingPlains: // Rolling Plains — wide open terrain with mild variation
            params.noiseType = NoiseType::Perlin;
            params.fractalType = FractalType::FBM;
            params.octaves = 6;
            params.frequency = 0.003f;
            params.amplitude = 0.8f;
            params.lacunarity = 2.2f;
            params.persistence = 0.4f;
            params.heightExponent = 0.6f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 30.0f;
            params.domainWarp.frequency = 0.003f;
            break;

        case HeightmapPreset::Mountains: // Mountains — dramatic terrain with peaks
            params.noiseType = NoiseType::Simplex;
            params.fractalType = FractalType::Ridged;
            params.octaves = 8;
            params.frequency = 0.004f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.2f;
            params.persistence = 0.5f;
            params.heightExponent = 1.4f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 60.0f;
            params.domainWarp.frequency = 0.004f;
            break;

        case HeightmapPreset::SharpPeaks: // Sharp Peaks — aggressive jagged mountains
            params.noiseType = NoiseType::Perlin;
            params.fractalType = FractalType::Ridged;
            params.octaves = 10;
            params.frequency = 0.006f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.5f;
            params.persistence = 0.55f;
            params.heightExponent = 2.0f;
            break;

        case HeightmapPreset::DeepValleys: // Deep Valleys — inverted ridged for canyon-like terrain
            params.noiseType = NoiseType::Simplex;
            params.fractalType = FractalType::Ridged;
            params.octaves = 8;
            params.frequency = 0.004f;
            params.amplitude = 1.0f;
            params.lacunarity = 2.0f;
            params.persistence = 0.5f;
            params.heightExponent = 1.5f;
            params.invert = true;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 40.0f;
            params.domainWarp.frequency = 0.003f;
            break;

        case HeightmapPreset::Plateaus: // Plateaus — flat-topped mesa terrain
            params.noiseType = NoiseType::Perlin;
            params.fractalType = FractalType::FBM;
            params.octaves = 5;
            params.frequency = 0.003f;
            params.amplitude = 0.8f;
            params.lacunarity = 2.0f;
            params.persistence = 0.45f;
            params.heightExponent = 0.4f;
            params.terracing = true;
            params.terraceSteps = 6;
            break;

        case HeightmapPreset::Islands: // Islands — smooth rounded landmasses with low areas
            params.noiseType = NoiseType::Simplex;
            params.fractalType = FractalType::Billowy;
            params.octaves = 6;
            params.frequency = 0.003f;
            params.amplitude = 0.9f;
            params.lacunarity = 2.0f;
            params.persistence = 0.4f;
            params.heightExponent = 1.8f;
            params.domainWarp.enabled = true;
            params.domainWarp.amplitude = 50.0f;
            params.domainWarp.frequency = 0.002f;
            break;
        }
    }

    void scaleHeightmapFeatures(HeightmapParams& params, uint32_t resolution,
                                bool resolutionIndependent, float featureScale)
    {
        if (resolutionIndependent && resolution > 0)
        {
            const float reference = static_cast<float>(heightmapPresetReferenceResolution);
            const float pixels = static_cast<float>(resolution);
            params.frequency *= reference / pixels;
            params.domainWarp.frequency *= reference / pixels;
            params.domainWarp.amplitude *= pixels / reference;
        }

        if (featureScale > 0.0f)
        {
            params.frequency /= featureScale;
            params.domainWarp.frequency /= featureScale;
            params.domainWarp.amplitude *= featureScale;
        }
    }
}
