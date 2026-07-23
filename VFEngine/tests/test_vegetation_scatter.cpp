#include <doctest.h>

#include "vegetation/VegetationScatterTypes.hpp"
#include "vegetation/ScatterRuleEvaluator.hpp"
#include "vegetation/ScatterBaker.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "vegetation/VegetationSerializer.hpp"
#include "terrain/TileHeightSampler.hpp"
#include "terrain/TerrainWeightMap.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace vegetation;

namespace
{
    // Flat-terrain synthetic samplers for the baker.
    auto flatHeight = [](float, float) { return 0.0f; };
    auto flatNormal = [](float, float) { return glm::vec3(0.0f, 1.0f, 0.0f); };
    auto fullLayer = [](uint8_t, float, float) { return 1.0f; };

    ScatterProfile oneRuleProfile(float density, float spacing, float jitter)
    {
        ScatterProfile p;
        ScatterRule r;
        r.paletteEntryIndex = 0;
        r.density = density;
        r.spacing = spacing;
        r.positionJitter = jitter;
        p.rules.push_back(r);
        return p;
    }
}

TEST_SUITE("VegetationScatter")
{
    TEST_CASE("evaluator slope mask mirrors passesMasks semantics")
    {
        ScatterRule r;
        r.useSlopeMask = true;
        r.slopeMinCos = 0.5f; // reject steeper than 60 deg
        r.slopeMaxCos = 1.0f;

        ScatterSample flat{0, 0, 0, glm::vec3(0, 1, 0), 1.0f};      // normal.y = 1
        ScatterSample steep{0, 0, 0, glm::vec3(0.9f, 0.3f, 0), 1.0f}; // normal.y = 0.3
        CHECK(ScatterRuleEvaluator::passes(r, flat));
        CHECK_FALSE(ScatterRuleEvaluator::passes(r, steep));
    }

    TEST_CASE("evaluator height mask gates a world-Y band")
    {
        ScatterRule r;
        r.useHeightMask = true;
        r.heightMin = 0.0f;
        r.heightMax = 10.0f;

        ScatterSample inBand{0, 0, 5.0f, glm::vec3(0, 1, 0), 1.0f};
        ScatterSample above{0, 0, 15.0f, glm::vec3(0, 1, 0), 1.0f};
        CHECK(ScatterRuleEvaluator::passes(r, inBand));
        CHECK_FALSE(ScatterRuleEvaluator::passes(r, above));
    }

    TEST_CASE("evaluator noise mask agrees with valueNoise2D threshold")
    {
        ScatterRule r;
        r.useNoiseMask = true;
        r.noiseFrequency = 0.1f;
        r.noiseThreshold = 0.5f;
        r.noiseSeed = 1337;

        for (float x : {1.0f, 7.0f, 13.0f, 21.0f})
        {
            ScatterSample s{x, x * 0.5f, 0, glm::vec3(0, 1, 0), 1.0f};
            const float n = terrain::valueNoise2D(x * r.noiseFrequency, (x * 0.5f) * r.noiseFrequency, r.noiseSeed);
            CHECK(ScatterRuleEvaluator::passes(r, s) == (n >= r.noiseThreshold));
        }
    }

    TEST_CASE("evaluator layer mask gates on splat weight, with invert")
    {
        ScatterRule r;
        r.useLayerMask = true;
        r.layerWeightMin = 0.5f;

        ScatterSample high{0, 0, 0, glm::vec3(0, 1, 0), 0.7f};
        ScatterSample low{0, 0, 0, glm::vec3(0, 1, 0), 0.3f};
        CHECK(ScatterRuleEvaluator::passes(r, high));
        CHECK_FALSE(ScatterRuleEvaluator::passes(r, low));

        r.invertLayer = true; // exclusion: place where weight < min
        CHECK_FALSE(ScatterRuleEvaluator::passes(r, high));
        CHECK(ScatterRuleEvaluator::passes(r, low));
    }

    TEST_CASE("bake is deterministic (idempotent Regenerate)")
    {
        ScatterProfile p = oneRuleProfile(1.0f, 1.0f, 0.5f);
        std::vector<BillboardPaletteEntry> palette(1);

        auto a = bakeScatterForTile(p, palette, 1337u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto b = bakeScatterForTile(p, palette, 1337u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);

        REQUIRE(a.instances.size() == b.instances.size());
        REQUIRE(a.instances.size() > 0);
        for (size_t i = 0; i < a.instances.size(); ++i)
        {
            CHECK(a.instances[i].position.x == doctest::Approx(b.instances[i].position.x));
            CHECK(a.instances[i].position.z == doctest::Approx(b.instances[i].position.z));
            CHECK(a.instances[i].rotation == doctest::Approx(b.instances[i].rotation));
            CHECK(a.instances[i].scale == doctest::Approx(b.instances[i].scale));
            CHECK(a.instances[i].heightScale == doctest::Approx(b.instances[i].heightScale));
            CHECK(a.instances[i].tint == doctest::Approx(b.instances[i].tint));
            CHECK(a.instances[i].windPhase == doctest::Approx(b.instances[i].windPhase));
            CHECK(a.instances[i].paletteEntryIndex == b.instances[i].paletteEntryIndex);
        }
    }

    TEST_CASE("different seed changes placement")
    {
        ScatterProfile p = oneRuleProfile(1.0f, 1.0f, 0.5f);
        std::vector<BillboardPaletteEntry> palette(1);

        auto a = bakeScatterForTile(p, palette, 1u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto b = bakeScatterForTile(p, palette, 2u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);

        REQUIRE(a.instances.size() == b.instances.size()); // same count on a flat field, density 1
        bool anyDiff = false;
        for (size_t i = 0; i < a.instances.size(); ++i)
            if (a.instances[i].position.x != doctest::Approx(b.instances[i].position.x))
                anyDiff = true;
        CHECK(anyDiff);
    }

    TEST_CASE("tile boundary is seam-free (cell-center ownership)")
    {
        ScatterProfile p = oneRuleProfile(1.0f, 1.0f, 0.5f);
        std::vector<BillboardPaletteEntry> palette(1);

        // One 16x16 region vs four adjacent 8x8 quadrant tiles covering the same area.
        // Tiles are square, so it takes four quadrants (not two halves) to tile a 16x16 span.
        auto big = bakeScatterForTile(p, palette, 42u, 0, 0, 16.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto q00 = bakeScatterForTile(p, palette, 42u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto q10 = bakeScatterForTile(p, palette, 42u, 8.0f, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto q01 = bakeScatterForTile(p, palette, 42u, 0, 8.0f, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);
        auto q11 = bakeScatterForTile(p, palette, 42u, 8.0f, 8.0f, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);

        std::vector<BillboardInstance> merged = q00.instances;
        merged.insert(merged.end(), q10.instances.begin(), q10.instances.end());
        merged.insert(merged.end(), q01.instances.begin(), q01.instances.end());
        merged.insert(merged.end(), q11.instances.begin(), q11.instances.end());

        CHECK(merged.size() == big.instances.size());

        auto keys = [](const std::vector<BillboardInstance>& v) {
            std::vector<std::pair<float, float>> k;
            for (const auto& i : v) k.emplace_back(i.position.x, i.position.z);
            std::sort(k.begin(), k.end());
            return k;
        };
        CHECK(keys(merged) == keys(big.instances));
    }

    TEST_CASE("approx min-spacing >= spacing*(1-jitter)")
    {
        const float spacing = 1.0f, jitter = 0.5f;
        ScatterProfile p = oneRuleProfile(1.0f, spacing, jitter);
        std::vector<BillboardPaletteEntry> palette(1);
        auto r = bakeScatterForTile(p, palette, 7u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, 1u << 20);

        const float minAllowed = spacing * (1.0f - jitter) - 1e-3f;
        for (size_t i = 0; i < r.instances.size(); ++i)
            for (size_t j = i + 1; j < r.instances.size(); ++j)
            {
                const float dx = r.instances[i].position.x - r.instances[j].position.x;
                const float dz = r.instances[i].position.z - r.instances[j].position.z;
                CHECK(std::sqrt(dx * dx + dz * dz) >= minAllowed);
            }
    }

    TEST_CASE("density controls coverage")
    {
        std::vector<BillboardPaletteEntry> palette(1);

        auto empty = bakeScatterForTile(oneRuleProfile(0.0f, 1.0f, 0.5f), palette, 5u, 0, 0, 8.0f,
                                        flatHeight, flatNormal, fullLayer, 1u << 20);
        CHECK(empty.instances.empty());

        auto full = bakeScatterForTile(oneRuleProfile(1.0f, 1.0f, 0.5f), palette, 5u, 0, 0, 8.0f,
                                       flatHeight, flatNormal, fullLayer, 1u << 20);
        CHECK(full.instances.size() == 64); // 8x8 cells, one per cell

        auto half = bakeScatterForTile(oneRuleProfile(0.5f, 1.0f, 0.5f), palette, 5u, 0, 0, 8.0f,
                                       flatHeight, flatNormal, fullLayer, 1u << 20);
        CHECK(half.instances.size() > 20);
        CHECK(half.instances.size() < 48);
    }

    TEST_CASE("baked instances are tagged Procedural")
    {
        std::vector<BillboardPaletteEntry> palette(1);
        auto r = bakeScatterForTile(oneRuleProfile(1.0f, 1.0f, 0.5f), palette, 9u, 0, 0, 8.0f,
                                    flatHeight, flatNormal, fullLayer, 1u << 20);
        REQUIRE(r.instances.size() > 0);
        for (const auto& i : r.instances)
            CHECK(i.source == InstanceSource::Procedural);
    }

    TEST_CASE("budget guard stops generation and flags overflow")
    {
        std::vector<BillboardPaletteEntry> palette(1);
        auto r = bakeScatterForTile(oneRuleProfile(1.0f, 1.0f, 0.0f), palette, 3u, 0, 0, 8.0f,
                                    flatHeight, flatNormal, fullLayer, /*budget*/ 10);
        CHECK(r.instances.size() == 10);
        CHECK(r.budgetExceeded);
    }

    TEST_CASE("VFVI v3 round-trip preserves source")
    {
        std::vector<BillboardInstance> in(2);
        in[0].position = {1, 2, 3};
        in[0].rotation = 0.4f;
        in[0].scale = 1.5f;
        in[0].paletteEntryIndex = 2;
        in[0].heightScale = 1.2f;
        in[0].tint = 0.8f;
        in[0].normal = {0, 1, 0};
        in[0].source = InstanceSource::Procedural;
        in[1].source = InstanceSource::Painted;

        auto path = (std::filesystem::temp_directory_path() / "vk1581_v3.vfVegInstances").string();
        REQUIRE(VegetationSerializer::saveBillboardInstances(path, in));

        std::vector<BillboardInstance> out;
        REQUIRE(VegetationSerializer::loadBillboardInstances(path, out));
        REQUIRE(out.size() == 2);
        CHECK(out[0].source == InstanceSource::Procedural);
        CHECK(out[1].source == InstanceSource::Painted);
        CHECK(out[0].position.x == doctest::Approx(1.0f));
        CHECK(out[0].paletteEntryIndex == 2);
        CHECK(out[0].tint == doctest::Approx(0.8f));
        std::filesystem::remove(path);
    }

    TEST_CASE("VFVI v2 file loads with source defaulted to Painted")
    {
        auto path = (std::filesystem::temp_directory_path() / "vk1581_v2.vfVegInstances").string();
        {
            std::ofstream f(path, std::ios::binary);
            const std::array<char, 4> magic = {'V', 'F', 'V', 'I'};
            f.write(magic.data(), 4);
            uint32_t version = 2;
            f.write(reinterpret_cast<const char*>(&version), sizeof(version));
            uint32_t count = 1;
            f.write(reinterpret_cast<const char*>(&count), sizeof(count));
            glm::vec3 pos{4, 5, 6};
            float rotation = 0.2f, scale = 2.0f, heightScale = 1.1f, tint = 0.9f;
            uint32_t palette = 3;
            glm::vec3 normal{0, 1, 0};
            f.write(reinterpret_cast<const char*>(&pos), sizeof(glm::vec3));
            f.write(reinterpret_cast<const char*>(&rotation), sizeof(float));
            f.write(reinterpret_cast<const char*>(&scale), sizeof(float));
            f.write(reinterpret_cast<const char*>(&palette), sizeof(uint32_t));
            f.write(reinterpret_cast<const char*>(&heightScale), sizeof(float));
            f.write(reinterpret_cast<const char*>(&tint), sizeof(float));
            f.write(reinterpret_cast<const char*>(&normal), sizeof(glm::vec3));
        }

        std::vector<BillboardInstance> out;
        REQUIRE(VegetationSerializer::loadBillboardInstances(path, out));
        REQUIRE(out.size() == 1);
        CHECK(out[0].source == InstanceSource::Painted); // v2 default
        CHECK(out[0].position.x == doctest::Approx(4.0f));
        CHECK(out[0].paletteEntryIndex == 3);
        std::filesystem::remove(path);
    }

    TEST_CASE("VFVI rejects bad magic and unknown version")
    {
        auto badMagic = (std::filesystem::temp_directory_path() / "vk1581_bad.vfVegInstances").string();
        {
            std::ofstream f(badMagic, std::ios::binary);
            const char junk[4] = {'X', 'X', 'X', 'X'};
            f.write(junk, 4);
            uint32_t version = 3, count = 0;
            f.write(reinterpret_cast<const char*>(&version), sizeof(version));
            f.write(reinterpret_cast<const char*>(&count), sizeof(count));
        }
        std::vector<BillboardInstance> out;
        CHECK_FALSE(VegetationSerializer::loadBillboardInstances(badMagic, out));
        std::filesystem::remove(badMagic);

        auto badVer = (std::filesystem::temp_directory_path() / "vk1581_ver.vfVegInstances").string();
        {
            std::ofstream f(badVer, std::ios::binary);
            const std::array<char, 4> magic = {'V', 'F', 'V', 'I'};
            f.write(magic.data(), 4);
            uint32_t version = 99, count = 0;
            f.write(reinterpret_cast<const char*>(&version), sizeof(version));
            f.write(reinterpret_cast<const char*>(&count), sizeof(count));
        }
        CHECK_FALSE(VegetationSerializer::loadBillboardInstances(badVer, out));
        std::filesystem::remove(badVer);
    }

    TEST_CASE("tile height sampler is bilinear")
    {
        // vpt=2, vertexSpacing=1 => 1x1 quad. heightData[z*vpt+x].
        std::array<float, 4> h = {0, 0, 0, 4}; // (0,0)=0 (1,0)=0 (0,1)=0 (1,1)=4
        CHECK(terrain::sampleTileHeightBilinear(h.data(), 2, 1.0f, 0.5f, 0.5f) == doctest::Approx(1.0f));
        CHECK(terrain::sampleTileHeightBilinear(h.data(), 2, 1.0f, 1.0f, 1.0f) == doctest::Approx(4.0f));
        // clamps outside the tile
        CHECK(terrain::sampleTileHeightBilinear(h.data(), 2, 1.0f, 5.0f, 5.0f) == doctest::Approx(4.0f));
    }

    TEST_CASE("tile layer weight sampler resolves channel and bilerps, 0 when absent")
    {
        terrain::TileWeightMapData wm;
        wm.resolution = 2;
        wm.layerWeights.assign(terrain::WEIGHT_CHANNELS, std::vector<float>(4, 0.0f));
        wm.layerIndices = {5, 1, 2, 3, 4, 0, 6, 7}; // channel 0 -> palette layer 5
        // channel 0 weights: (1,1)=1, rest 0
        wm.layerWeights[0][1 * 2 + 1] = 1.0f;

        CHECK(terrain::sampleTileLayerWeightBilinear(wm, 5, 1.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
        CHECK(terrain::sampleTileLayerWeightBilinear(wm, 5, 0.0f, 0.0f, 1.0f) == doctest::Approx(0.0f));
        // layer 30 not present on any channel -> weight 0
        CHECK(terrain::sampleTileLayerWeightBilinear(wm, 30, 1.0f, 1.0f, 1.0f) == doctest::Approx(0.0f));
    }
}
