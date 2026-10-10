#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// VK-1653 procedural heightmap generation requests (MCP terrain_generate_heightmap).
//
// Plain data: the generator lives in ProceduralGen.dll, which only the Editor links, so the request
// is handled by editor/handlers/HeightmapGenerationHandler and nothing here names a procedural:: type.
// Enum fields use the procedural::NoiseType / FractalType integer values.

namespace services
{
    // Every noise setting the generator takes, fully resolved (preset -> overrides -> resolution
    // scaling -> featureScale). Reported back so the caller can reproduce or tweak a result.
    struct HeightmapNoiseSettings
    {
        uint8_t noiseType = 0;   // 0 Perlin, 1 Simplex
        uint8_t fractalType = 1; // 0 None, 1 FBM, 2 Ridged, 3 Billowy
        int octaves = 4;
        float frequency = 0.002f; // per heightmap PIXEL (after resolution scaling)
        float lacunarity = 2.0f;
        float persistence = 0.35f;
        float heightExponent = 1.0f;
        bool domainWarp = false;
        float warpAmplitude = 50.0f;
        float warpFrequency = 0.005f;
        bool invert = false;
        bool terracing = false;
        int terraceSteps = 8;
    };

    struct HeightmapGenerationRequest
    {
        std::string outputPath; // absolute .vfImage
        bool overwrite = false;

        // A HeightmapPresets id ("hills", "plains", "mountains", "peaks", "valleys", "plateaus",
        // "islands") or "custom" (generator defaults + overrides).
        std::string preset = "hills";
        uint32_t resolution = 1024; // square
        uint32_t seed = 42;

        // Presets are tuned at the generator window's default 4096 resolution, and frequency is
        // measured per pixel. With resolutionIndependent the frequency/warp settings are rescaled
        // to `resolution` so a preset keeps its look; featureScale > 1 then makes features larger.
        bool resolutionIndependent = true;
        float featureScale = 1.0f;

        // Overrides applied after the preset (and before resolution scaling).
        std::optional<uint8_t> noiseType;
        std::optional<uint8_t> fractalType;
        std::optional<int> octaves;
        std::optional<float> frequency;
        std::optional<float> lacunarity;
        std::optional<float> persistence;
        std::optional<float> heightExponent;
        std::optional<bool> domainWarp;
        std::optional<float> warpAmplitude;
        std::optional<float> warpFrequency;
        std::optional<bool> invert;
        std::optional<bool> terracing;
        std::optional<int> terraceSteps;
    };

    struct HeightmapJobStart
    {
        bool accepted = false;
        uint64_t jobId = 0;
        std::string error;
    };

    enum class HeightmapJobState : uint8_t
    {
        Unknown = 0, // no such job (never started, or evicted from the result history)
        Running,
        Done,
        Failed
    };

    struct HeightmapJobStatus
    {
        HeightmapJobState state = HeightmapJobState::Unknown;
        float progress = 0.0f;
        std::string outputPath;
        uint32_t width = 0;
        uint32_t height = 0;
        HeightmapNoiseSettings effective;
        double durationMs = 0.0;
        std::string error;

        // Done only: a previewSize x previewSize RGBA8 preview (R = G = B = height).
        std::vector<uint8_t> previewRgba;
        uint32_t previewSize = 0;
    };
}
