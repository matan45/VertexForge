#include <doctest.h>

#include "procedural/generator/HeightmapPresets.hpp"
#include "tools/TerrainToolSupport.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>  // doctest stringifies std::string_view operands via operator<<
#include <string_view>

// VK-1653. HeightmapGeneratorWindow::applyPreset moved into ProceduralGen's HeightmapPresets table so
// the MCP heightmap tool shares it. These pin the moved values to the original window code, the ids
// MCP exposes, and the resolution / featureScale rescaling HeightmapGenerationHandler applies.

namespace
{
    using procedural::FractalType;
    using procedural::HeightmapParams;
    using procedural::HeightmapPreset;
    using procedural::NoiseType;

    // Every field the window's applyPreset set, copied from it before the move; the comment is the
    // window's combo index ("Custom" is 0).
    struct PresetGolden
    {
        const char* id;
        const char* displayName;
        HeightmapPreset preset;
        NoiseType noiseType;
        FractalType fractalType;
        int octaves;
        float frequency;
        float amplitude;
        float lacunarity;
        float persistence;
        float heightExponent;
        bool domainWarp;
        float warpAmplitude; // only set when domainWarp
        float warpFrequency; // only set when domainWarp
        bool invert;
        bool terracing;
        int terraceSteps;
    };

    constexpr std::array<PresetGolden, 7> goldens = {{
        // 1
        { "hills", "Flat Hills", HeightmapPreset::FlatHills, NoiseType::Simplex, FractalType::FBM,
          4, 0.002f, 0.6f, 2.0f, 0.35f, 0.7f, false, 0.0f, 0.0f, false, false, 8 },
        // 2
        { "plains", "Rolling Plains", HeightmapPreset::RollingPlains, NoiseType::Perlin, FractalType::FBM,
          6, 0.003f, 0.8f, 2.2f, 0.4f, 0.6f, true, 30.0f, 0.003f, false, false, 8 },
        // 3
        { "mountains", "Mountains", HeightmapPreset::Mountains, NoiseType::Simplex, FractalType::Ridged,
          8, 0.004f, 1.0f, 2.2f, 0.5f, 1.4f, true, 60.0f, 0.004f, false, false, 8 },
        // 4
        { "peaks", "Sharp Peaks", HeightmapPreset::SharpPeaks, NoiseType::Perlin, FractalType::Ridged,
          10, 0.006f, 1.0f, 2.5f, 0.55f, 2.0f, false, 0.0f, 0.0f, false, false, 8 },
        // 5
        { "valleys", "Deep Valleys", HeightmapPreset::DeepValleys, NoiseType::Simplex, FractalType::Ridged,
          8, 0.004f, 1.0f, 2.0f, 0.5f, 1.5f, true, 40.0f, 0.003f, true, false, 8 },
        // 6
        { "plateaus", "Plateaus", HeightmapPreset::Plateaus, NoiseType::Perlin, FractalType::FBM,
          5, 0.003f, 0.8f, 2.0f, 0.45f, 0.4f, false, 0.0f, 0.0f, false, true, 6 },
        // 7
        { "islands", "Islands", HeightmapPreset::Islands, NoiseType::Simplex, FractalType::Billowy,
          6, 0.003f, 0.9f, 2.0f, 0.4f, 1.8f, true, 50.0f, 0.002f, false, false, 8 },
    }};

    // Two starting states that differ in every field, so an assignment or reset the move dropped
    // leaves a stale value behind in at least one of them.
    HeightmapParams startA()
    {
        HeightmapParams params;
        params.width = 333;
        params.height = 777;
        params.seed = 9001;
        params.noiseType = NoiseType::Perlin;
        params.fractalType = FractalType::None;
        params.octaves = 13;
        params.frequency = 0.0777f;
        params.amplitude = 0.123f;
        params.lacunarity = 3.3f;
        params.persistence = 0.987f;
        params.domainWarp.enabled = true;
        params.domainWarp.amplitude = 123.0f;
        params.domainWarp.frequency = 0.0456f;
        params.heightExponent = 4.4f;
        params.invert = true;
        params.terracing = true;
        params.terraceSteps = 33;
        return params;
    }

    HeightmapParams startB()
    {
        HeightmapParams params;
        params.width = 64;
        params.height = 128;
        params.seed = 1;
        params.noiseType = NoiseType::Simplex;
        params.fractalType = FractalType::Billowy;
        params.octaves = 2;
        params.frequency = 0.0011f;
        params.amplitude = 0.01f;
        params.lacunarity = 1.1f;
        params.persistence = 0.01f;
        params.domainWarp.enabled = false;
        params.domainWarp.amplitude = 7.0f;
        params.domainWarp.frequency = 0.0099f;
        params.heightExponent = 0.15f;
        params.invert = false;
        params.terracing = false;
        params.terraceSteps = 50;
        return params;
    }

    void checkPreset(const PresetGolden& golden, const HeightmapParams& start)
    {
        HeightmapParams params = start;
        procedural::applyHeightmapPreset(golden.preset, params);

        CHECK(params.noiseType == golden.noiseType);
        CHECK(params.fractalType == golden.fractalType);
        CHECK(params.octaves == golden.octaves);
        CHECK(params.frequency == golden.frequency);
        CHECK(params.amplitude == golden.amplitude);
        CHECK(params.lacunarity == golden.lacunarity);
        CHECK(params.persistence == golden.persistence);
        CHECK(params.heightExponent == golden.heightExponent);
        CHECK(params.domainWarp.enabled == golden.domainWarp);
        CHECK(params.invert == golden.invert);
        CHECK(params.terracing == golden.terracing);
        CHECK(params.terraceSteps == golden.terraceSteps);

        if (golden.domainWarp)
        {
            CHECK(params.domainWarp.amplitude == golden.warpAmplitude);
            CHECK(params.domainWarp.frequency == golden.warpFrequency);
        }
        else
        {
            // The window left the warp shape alone when a preset switched warping off.
            CHECK(params.domainWarp.amplitude == start.domainWarp.amplitude);
            CHECK(params.domainWarp.frequency == start.domainWarp.frequency);
        }

        // Resolution and seed belong to the caller.
        CHECK(params.width == start.width);
        CHECK(params.height == start.height);
        CHECK(params.seed == start.seed);
    }

    HeightmapParams scalingBase()
    {
        HeightmapParams params;
        params.width = 4096;
        params.height = 4096;
        params.seed = 7;
        params.octaves = 8;
        params.frequency = 0.004f;
        params.lacunarity = 2.2f;
        params.persistence = 0.5f;
        params.domainWarp.enabled = true;
        params.domainWarp.amplitude = 60.0f;
        params.domainWarp.frequency = 0.002f;
        return params;
    }

    HeightmapParams scaled(uint32_t resolution, bool resolutionIndependent, float featureScale)
    {
        HeightmapParams params = scalingBase();
        procedural::scaleHeightmapFeatures(params, resolution, resolutionIndependent, featureScale);
        return params;
    }

    void checkUnscaled(const HeightmapParams& params)
    {
        const HeightmapParams base = scalingBase();
        CHECK(params.frequency == base.frequency);
        CHECK(params.domainWarp.frequency == base.domainWarp.frequency);
        CHECK(params.domainWarp.amplitude == base.domainWarp.amplitude);
    }
}

TEST_CASE("VK-1653 heightmap presets: ids and labels follow the generator window's combo order")
{
    const auto presets = procedural::heightmapPresets();
    REQUIRE(presets.size() == goldens.size());

    for (std::size_t i = 0; i < goldens.size(); ++i)
    {
        CAPTURE(i);
        CHECK(presets[i].preset == goldens[i].preset);
        CHECK(std::string_view(presets[i].id) == std::string_view(goldens[i].id));
        CHECK(std::string_view(presets[i].displayName) == std::string_view(goldens[i].displayName));
    }
}

TEST_CASE("VK-1653 heightmap presets: applyHeightmapPreset reproduces the old window values")
{
    for (const auto& golden : goldens)
    {
        INFO("preset " << golden.id);
        checkPreset(golden, startA());
        checkPreset(golden, startB());
    }
}

TEST_CASE("VK-1653 heightmap presets: lookup by id is exact")
{
    for (const auto& golden : goldens)
    {
        CAPTURE(golden.id);
        const auto found = procedural::findHeightmapPreset(golden.id);
        REQUIRE(found.has_value());
        CHECK(*found == golden.preset);
    }

    // "custom" is the handler's "no preset", not a table row.
    CHECK_FALSE(procedural::findHeightmapPreset("custom").has_value());
    CHECK_FALSE(procedural::findHeightmapPreset("Hills").has_value());
    CHECK_FALSE(procedural::findHeightmapPreset("Flat Hills").has_value());
    CHECK_FALSE(procedural::findHeightmapPreset("hills ").has_value());
    CHECK_FALSE(procedural::findHeightmapPreset("").has_value());
}

TEST_CASE("VK-1653 heightmap presets: ids are single lowercase words")
{
    // MCP exposes them as an enum, and test_mcp_prompts reads every snake_case token in prompt text
    // as a tool name.
    for (const auto& preset : procedural::heightmapPresets())
    {
        const std::string_view id = preset.id;
        CAPTURE(id);
        CHECK_FALSE(id.empty());
        CHECK(std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'z'; }));
    }
}

TEST_CASE("VK-1653 heightmap presets: the MCP preset enum is the ProceduralGen table, in order")
{
    const auto presets = procedural::heightmapPresets();
    const auto& mcpIds = mcp::tools::heightmapPresetIds();
    REQUIRE(std::size(mcpIds) == presets.size());

    std::size_t i = 0;
    for (const auto& id : mcpIds)
    {
        CAPTURE(i);
        CHECK(std::string_view(id) == std::string_view(presets[i].id));
        ++i;
    }
}

TEST_CASE("VK-1653 heightmap presets: scaleHeightmapFeatures keeps a preset's look at any resolution")
{
    CHECK(procedural::heightmapPresetReferenceResolution == 4096u);

    SUBCASE("the reference resolution at featureScale 1 is the identity")
    {
        checkUnscaled(scaled(4096, true, 1.0f));
    }

    SUBCASE("a quarter of the resolution needs 4x the frequency and a quarter of the warp")
    {
        const HeightmapParams params = scaled(1024, true, 1.0f);
        CHECK(params.frequency == doctest::Approx(0.016));
        CHECK(params.domainWarp.frequency == doctest::Approx(0.008));
        CHECK(params.domainWarp.amplitude == doctest::Approx(15.0));
    }

    SUBCASE("twice the resolution halves the frequency and doubles the warp")
    {
        const HeightmapParams params = scaled(8192, true, 1.0f);
        CHECK(params.frequency == doctest::Approx(0.002));
        CHECK(params.domainWarp.frequency == doctest::Approx(0.001));
        CHECK(params.domainWarp.amplitude == doctest::Approx(120.0));
    }

    SUBCASE("a resolution that is not a power of two")
    {
        const HeightmapParams params = scaled(1000, true, 1.0f);
        CHECK(params.frequency == doctest::Approx(0.004 * 4.096));
        CHECK(params.domainWarp.frequency == doctest::Approx(0.002 * 4.096));
        CHECK(params.domainWarp.amplitude == doctest::Approx(60.0 * 1000.0 / 4096.0));
    }

    SUBCASE("resolutionIndependent off keeps the per-pixel values")
    {
        checkUnscaled(scaled(1024, false, 1.0f));
    }

    SUBCASE("featureScale > 1 makes every feature larger")
    {
        const HeightmapParams params = scaled(4096, true, 2.0f);
        CHECK(params.frequency == doctest::Approx(0.002));
        CHECK(params.domainWarp.frequency == doctest::Approx(0.001));
        CHECK(params.domainWarp.amplitude == doctest::Approx(120.0));
    }

    SUBCASE("resolution and featureScale compose")
    {
        // 2048 doubles the frequency, featureScale 4 quarters it; the warp goes the other way.
        const HeightmapParams params = scaled(2048, true, 4.0f);
        CHECK(params.frequency == doctest::Approx(0.002));
        CHECK(params.domainWarp.frequency == doctest::Approx(0.001));
        CHECK(params.domainWarp.amplitude == doctest::Approx(120.0));
    }

    SUBCASE("a zero resolution or a non-positive featureScale skips its step")
    {
        checkUnscaled(scaled(0, true, 1.0f));
        checkUnscaled(scaled(4096, true, 0.0f));
        checkUnscaled(scaled(4096, true, -2.0f));
    }

    SUBCASE("nothing but the frequencies and the warp amplitude changes")
    {
        const HeightmapParams base = scalingBase();
        const HeightmapParams params = scaled(1000, true, 3.0f);
        CHECK(params.width == base.width);
        CHECK(params.height == base.height);
        CHECK(params.seed == base.seed);
        CHECK(params.octaves == base.octaves);
        CHECK(params.lacunarity == base.lacunarity);
        CHECK(params.persistence == base.persistence);
        CHECK(params.domainWarp.enabled == base.domainWarp.enabled);
    }
}
