#include <doctest.h>

// VK-1585: biome layering — the pure compositor math (continuity/priority) plus the biome bake
// path through the deterministic billboard baker (determinism, layer gating, seam-free partition).

#include "vegetation/VegetationScatterTypes.hpp"
#include "vegetation/BiomeCompositor.hpp"
#include "vegetation/ScatterBaker.hpp"
#include "vegetation/VegetationTypes.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

using namespace vegetation;

namespace
{
    auto flatHeight = [](float, float) { return 0.0f; };
    auto flatNormal = [](float, float) { return glm::vec3(0.0f, 1.0f, 0.0f); };
    auto zeroCurv = [](float, float) { return 0.0f; };
    auto fullLayer = [](uint8_t, float, float) { return 1.0f; };
}

TEST_SUITE("BiomeCompositor")
{
    TEST_CASE("biomeMembership is a monotone smoothstep with exact endpoints")
    {
        CHECK(biomeMembership(0.0f, 0.3f) == doctest::Approx(0.0f));
        CHECK(biomeMembership(1.0f, 0.3f) == doctest::Approx(1.0f));
        CHECK(biomeMembership(0.5f, 0.3f) == doctest::Approx(0.5f));

        float prev = -1.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float m = biomeMembership(i / 10.0f, 0.4f);
            CHECK(m >= prev - 1e-6f); // non-decreasing
            prev = m;
        }
    }

    TEST_CASE("edgeBlendWidth 0 is a hard step at 0.5")
    {
        CHECK(biomeMembership(0.49f, 0.0f) == doctest::Approx(0.0f));
        CHECK(biomeMembership(0.51f, 0.0f) == doctest::Approx(1.0f));
    }

    TEST_CASE("biomeEffectiveKeepProb: interior full, higher-priority suppresses, clamped")
    {
        CHECK(biomeEffectiveKeepProb(0.8f, 1.0f, 1.0f, 1.0f, 0.0f) == doctest::Approx(0.8f)); // interior
        CHECK(biomeEffectiveKeepProb(0.8f, 1.0f, 1.0f, 1.0f, 1.0f) == doctest::Approx(0.0f)); // suppressed
        CHECK(biomeEffectiveKeepProb(2.0f, 2.0f, 2.0f, 1.0f, 0.0f) == doctest::Approx(1.0f)); // clamped
        CHECK(biomeEffectiveKeepProb(1.0f, 0.5f, 0.5f, 1.0f, 0.0f) == doctest::Approx(0.25f)); // scales multiply
    }

    TEST_CASE("empty biomes bake == flat-rules-only (legacy behaviour preserved)")
    {
        ScatterProfile p;
        ScatterRule r; r.density = 1.0f; r.spacing = 1.0f; r.positionJitter = 0.5f;
        p.rules.push_back(r);
        std::vector<BillboardPaletteEntry> palette(1);

        auto a = bakeScatterForTile(p, palette, 1337u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        CHECK(a.instances.size() == 64); // 8x8, density 1 — unchanged from VK-1581
    }

    TEST_CASE("biome bake is deterministic and gated by the biome layer")
    {
        // Synthetic layer: left world-x half weight 1, right half 0 (tile origin 0).
        auto leftLayer = [](uint8_t, float lx, float) { return lx < 4.0f ? 1.0f : 0.0f; };

        ScatterProfile p;
        BiomeLayer biome;
        biome.biomeLayerIndex = 3;
        biome.edgeBlendWidth = 0.0f; // hard step at weight 0.5
        ScatterRule r; r.density = 1.0f; r.spacing = 1.0f; r.positionJitter = 0.0f;
        biome.rules.push_back(r);
        p.biomes.push_back(biome);
        std::vector<BillboardPaletteEntry> palette(1);

        auto a = bakeScatterForTile(p, palette, 55u, 0, 0, 8.0f, flatHeight, flatNormal, leftLayer, zeroCurv, 1u << 20);
        auto b = bakeScatterForTile(p, palette, 55u, 0, 0, 8.0f, flatHeight, flatNormal, leftLayer, zeroCurv, 1u << 20);

        REQUIRE(!a.instances.empty());
        REQUIRE(a.instances.size() == b.instances.size());
        for (const auto& i : a.instances)
            CHECK(i.position.x < 4.0f); // only the biome region places
        for (size_t i = 0; i < a.instances.size(); ++i)
            CHECK(a.instances[i].position.x == doctest::Approx(b.instances[i].position.x));
    }

    TEST_CASE("higher-priority biome suppresses a lower one in full overlap")
    {
        ScatterProfile p;
        BiomeLayer lo; lo.priority = 0;  lo.edgeBlendWidth = 0.0f;
        { ScatterRule r; r.density = 1.0f; r.spacing = 1.0f; r.positionJitter = 0.0f; lo.rules.push_back(r); }
        BiomeLayer hi; hi.priority = 10; hi.edgeBlendWidth = 0.0f;
        { ScatterRule r; r.density = 1.0f; r.spacing = 1.0f; r.positionJitter = 0.0f; hi.rules.push_back(r); }
        p.biomes.push_back(lo);
        p.biomes.push_back(hi);
        std::vector<BillboardPaletteEntry> palette(1);

        // Both biomes cover the whole tile (weight 1 everywhere). The low-priority biome is fully
        // suppressed (mHi=1 -> keepProb 0); only the high biome's 64 cells place — NOT 128.
        auto r = bakeScatterForTile(p, palette, 7u, 0, 0, 8.0f, flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        CHECK(r.instances.size() == 64);
    }

    TEST_CASE("biome bake is tile-partition independent (seam-free)")
    {
        ScatterProfile p;
        BiomeLayer biome; biome.biomeLayerIndex = 1; biome.edgeBlendWidth = 0.0f;
        ScatterRule r; r.density = 1.0f; r.spacing = 1.0f; r.positionJitter = 0.5f;
        biome.rules.push_back(r);
        p.biomes.push_back(biome);
        std::vector<BillboardPaletteEntry> palette(1);

        // World-space membership (left world-x half is the biome). Each bake gets a layerFn that
        // maps its own tile-local x back to world x via the captured origin — so membership is a
        // function of WORLD position and is consistent across the partition.
        auto layerAt = [](float originX) {
            return [originX](uint8_t, float lx, float) { return (lx + originX) < 8.0f ? 1.0f : 0.0f; };
        };

        auto big = bakeScatterForTile(p, palette, 42u, 0, 0, 16.0f, flatHeight, flatNormal, layerAt(0.0f), zeroCurv, 1u << 20);
        auto q00 = bakeScatterForTile(p, palette, 42u, 0, 0, 8.0f, flatHeight, flatNormal, layerAt(0.0f), zeroCurv, 1u << 20);
        auto q10 = bakeScatterForTile(p, palette, 42u, 8.0f, 0, 8.0f, flatHeight, flatNormal, layerAt(8.0f), zeroCurv, 1u << 20);
        auto q01 = bakeScatterForTile(p, palette, 42u, 0, 8.0f, 8.0f, flatHeight, flatNormal, layerAt(0.0f), zeroCurv, 1u << 20);
        auto q11 = bakeScatterForTile(p, palette, 42u, 8.0f, 8.0f, 8.0f, flatHeight, flatNormal, layerAt(8.0f), zeroCurv, 1u << 20);

        std::vector<BillboardInstance> merged = q00.instances;
        merged.insert(merged.end(), q10.instances.begin(), q10.instances.end());
        merged.insert(merged.end(), q01.instances.begin(), q01.instances.end());
        merged.insert(merged.end(), q11.instances.begin(), q11.instances.end());

        CHECK(merged.size() == big.instances.size());

        auto keys = [](std::vector<BillboardInstance> v) {
            std::vector<std::pair<float, float>> k;
            for (const auto& i : v) k.emplace_back(i.position.x, i.position.z);
            std::sort(k.begin(), k.end());
            return k;
        };
        CHECK(keys(merged) == keys(big.instances));
    }
}
