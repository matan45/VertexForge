#include <doctest.h>
#include <terrain/TerrainLayerVisibility.hpp>
#include <terrain/TerrainWeightMap.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include <terrain/WeightBrushApplicator.hpp> // Finding B: BrushShape::Square through the real apply()
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

// ============================================================
// VK-1613: per-layer visibility (the terrain material's `enabled` checkbox).
//
// The GPU half of this lives in TerrainGPUAdapter::uploadWeightMap, which needs a Vulkan device and
// so cannot be reached from here. What CAN be got wrong is the mask arithmetic and the channel ->
// palette-layer indirection, plus the promise that packing is untouched when nothing is hidden —
// that promise is what makes the feature free for materials that do not use it, so it is pinned here
// as an executable check rather than left as a comment.
// ============================================================

namespace
{
    // Mirror of the pack loop in TerrainGPUAdapter::uploadWeightMap. Kept deliberately literal (same
    // quantization expression, same channel-major layout) so a divergence in either shows up as a
    // failure here instead of as a visual artefact on a terrain nobody is looking at.
    std::vector<uint8_t> packWeights(const terrain::TileWeightMapData& wm, uint32_t mask)
    {
        std::array<bool, terrain::WEIGHT_CHANNELS> visible{};
        for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
        {
            visible[ch] = terrain::isWeightChannelEnabled(wm, ch, mask);
        }

        std::vector<uint8_t> out(static_cast<size_t>(wm.getTexelCount()) * terrain::WEIGHT_CHANNELS);
        for (uint32_t z = 0; z < wm.resolution; ++z)
        {
            for (uint32_t x = 0; x < wm.resolution; ++x)
            {
                const uint32_t pixelOffset = (z * wm.resolution + x) * terrain::WEIGHT_CHANNELS;
                for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
                {
                    out[pixelOffset + ch] = visible[ch]
                        ? static_cast<uint8_t>(wm.getWeight(ch, x, z) * 255.0f + 0.5f)
                        : uint8_t{0};
                }
            }
        }
        return out;
    }

    terrain::TerrainMaterialData makeMaterial(uint8_t activeLayers)
    {
        terrain::TerrainMaterialData material;
        material.activeLayerCount = activeLayers;
        return material;
    }
}

TEST_SUITE("TerrainLayerVisibility") {

// ---- Mask construction ----

TEST_CASE("buildLayerEnabledMask on an untouched material is exactly ALL_TERRAIN_LAYERS_ENABLED") {
    // The no-op-by-construction property: if this ever stops holding, every project that does not use
    // per-layer visibility starts paying for it.
    for (uint8_t activeLayers : {uint8_t{1}, uint8_t{4}, static_cast<uint8_t>(terrain::MAX_TERRAIN_LAYERS)})
    {
        auto material = makeMaterial(activeLayers);
        CHECK(terrain::buildLayerEnabledMask(material) == terrain::ALL_TERRAIN_LAYERS_ENABLED);
    }
}

TEST_CASE("buildLayerEnabledMask clears exactly the hidden layers' bits") {
    auto material = makeMaterial(4);

    SUBCASE("one hidden layer clears one bit") {
        material.layers[2].enabled = false;
        const uint32_t mask = terrain::buildLayerEnabledMask(material);

        CHECK(mask == (terrain::ALL_TERRAIN_LAYERS_ENABLED & ~(1u << 2)));
        CHECK(terrain::isLayerEnabled(mask, 0));
        CHECK(terrain::isLayerEnabled(mask, 1));
        CHECK_FALSE(terrain::isLayerEnabled(mask, 2));
        CHECK(terrain::isLayerEnabled(mask, 3));
    }

    SUBCASE("several hidden layers") {
        material.layers[0].enabled = false;
        material.layers[3].enabled = false;
        const uint32_t mask = terrain::buildLayerEnabledMask(material);

        CHECK_FALSE(terrain::isLayerEnabled(mask, 0));
        CHECK(terrain::isLayerEnabled(mask, 1));
        CHECK(terrain::isLayerEnabled(mask, 2));
        CHECK_FALSE(terrain::isLayerEnabled(mask, 3));
    }

    SUBCASE("every active layer hidden still leaves the inactive bits set") {
        for (int i = 0; i < 4; ++i) material.layers[i].enabled = false;
        const uint32_t mask = terrain::buildLayerEnabledMask(material);

        CHECK((mask & 0xFu) == 0u);
        CHECK(terrain::isLayerEnabled(mask, 4));
    }
}

TEST_CASE("buildLayerEnabledMask ignores `enabled` beyond activeLayerCount") {
    // A layer the artist removed by lowering activeLayerCount must not keep influencing anything —
    // otherwise re-raising the count would resurrect a stale hidden flag.
    auto material = makeMaterial(2);
    material.layers[5].enabled = false;
    material.layers[31].enabled = false;

    CHECK(terrain::buildLayerEnabledMask(material) == terrain::ALL_TERRAIN_LAYERS_ENABLED);
}

TEST_CASE("buildLayerEnabledMask survives an activeLayerCount past the layer array") {
    // activeLayerCount is a uint8_t and nothing structurally bounds it to MAX_TERRAIN_LAYERS, so a
    // corrupt or hand-edited asset must not walk off the array.
    auto material = makeMaterial(200);
    material.layers[1].enabled = false;

    const uint32_t mask = terrain::buildLayerEnabledMask(material);
    CHECK_FALSE(terrain::isLayerEnabled(mask, 1));
    CHECK(terrain::isLayerEnabled(mask, 0));
}

// ---- Palette indirection ----

TEST_CASE("isLayerEnabled reports out-of-range palette indices as visible") {
    // Palette indices are uint8_t, so they outrange the 32-bit mask; `1u << 32` would be UB. Such an
    // index cannot name a real layer, so today's behaviour is preserved rather than changed.
    const uint32_t allHidden = 0u;

    CHECK_FALSE(terrain::isLayerEnabled(allHidden, 0));
    CHECK_FALSE(terrain::isLayerEnabled(allHidden, static_cast<uint8_t>(terrain::MAX_TERRAIN_LAYERS - 1)));
    CHECK(terrain::isLayerEnabled(allHidden, static_cast<uint8_t>(terrain::MAX_TERRAIN_LAYERS)));
    CHECK(terrain::isLayerEnabled(allHidden, 255));
}

TEST_CASE("isWeightChannelEnabled resolves through TileWeightMapData::layerIndices") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(5);
    // Same shuffled palette shape test_vegetation_scatter uses, so both pin the identical contract.
    wm.layerIndices = {5, 1, 2, 3, 4, 0, 6, 7};

    SUBCASE("hiding palette layer 5 hides the channel that maps to it, not channel 5") {
        const uint32_t mask = terrain::ALL_TERRAIN_LAYERS_ENABLED & ~(1u << 5);

        CHECK_FALSE(terrain::isWeightChannelEnabled(wm, 0, mask)); // channel 0 -> palette layer 5
        CHECK(terrain::isWeightChannelEnabled(wm, 5, mask));       // channel 5 -> palette layer 0
        CHECK(terrain::isWeightChannelEnabled(wm, 1, mask));
    }

    SUBCASE("a palette index beyond the mask reads as visible") {
        wm.layerIndices[3] = 200;
        CHECK(terrain::isWeightChannelEnabled(wm, 3, 0u));
    }

    SUBCASE("a channel index past WEIGHT_CHANNELS does not read out of bounds") {
        CHECK(terrain::isWeightChannelEnabled(wm, terrain::WEIGHT_CHANNELS, 0u));
    }
}

// ---- The pack loop ----

TEST_CASE("packing is bit-identical when nothing is hidden") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(9);
    wm.setWeight(1, 2, 3, 0.25f);
    wm.setWeight(2, 2, 3, 0.5f);
    wm.normalizeAt(2, 3);

    const auto reference = packWeights(wm, terrain::ALL_TERRAIN_LAYERS_ENABLED);
    terrain::TerrainMaterialData material = makeMaterial(4);

    // Byte-for-byte, not approximately: the masked path must not perturb the quantization.
    CHECK(packWeights(wm, terrain::buildLayerEnabledMask(material)) == reference);
}

TEST_CASE("packing zeroes a hidden channel and leaves the rest untouched") {
    terrain::TileWeightMapData wm;
    wm.initializeDefault(5);
    wm.setWeight(0, 1, 1, 0.5f);
    wm.setWeight(1, 1, 1, 0.25f);
    wm.setWeight(2, 1, 1, 0.25f);

    const auto reference = packWeights(wm, terrain::ALL_TERRAIN_LAYERS_ENABLED);

    auto material = makeMaterial(3);
    material.layers[1].enabled = false;
    const auto masked = packWeights(wm, terrain::buildLayerEnabledMask(material));

    REQUIRE(masked.size() == reference.size());

    const uint32_t texel = (1 * wm.resolution + 1) * terrain::WEIGHT_CHANNELS;
    CHECK(masked[texel + 1] == 0);
    CHECK(reference[texel + 1] != 0); // the test would be vacuous if the source weight were already 0

    // Every other byte in the whole blob is identical — hiding a layer must not disturb its neighbours
    // on the CPU side; the redistribution happens on the GPU via the composite's weight normalization.
    bool othersUnchanged = true;
    for (size_t i = 0; i < masked.size(); ++i)
    {
        if (i % terrain::WEIGHT_CHANNELS == 1) continue;
        if (masked[i] != reference[i]) othersUnchanged = false;
    }
    CHECK(othersUnchanged);
}

TEST_CASE("hiding a layer leaves the surviving weights to renormalize to 1") {
    // Stands in for the shader's ls_InvW = 1/max(ls_TotalW, 0.001) step: the composite divides by the
    // weight that SURVIVED, so hiding one layer scales the others up rather than darkening the result.
    terrain::TileWeightMapData wm;
    wm.initializeDefault(5);
    wm.setWeight(0, 0, 0, 0.5f);
    wm.setWeight(1, 0, 0, 0.25f);
    wm.setWeight(2, 0, 0, 0.25f);

    auto material = makeMaterial(3);
    material.layers[1].enabled = false;
    const auto masked = packWeights(wm, terrain::buildLayerEnabledMask(material));

    float total = 0.0f;
    for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
    {
        total += static_cast<float>(masked[ch]) / 255.0f;
    }
    REQUIRE(total > 0.0f);

    const float survivorA = (static_cast<float>(masked[0]) / 255.0f) / total;
    const float survivorC = (static_cast<float>(masked[2]) / 255.0f) / total;

    // 0.5 and 0.25 with the 0.25 removed => 2/3 and 1/3.
    CHECK(survivorA == doctest::Approx(2.0f / 3.0f).epsilon(0.01f));
    CHECK(survivorC == doctest::Approx(1.0f / 3.0f).epsilon(0.01f));
    CHECK(survivorA + survivorC == doctest::Approx(1.0f).epsilon(0.001f));
}

// ---- VK-1613 Finding B: BrushShape::Square ----

TEST_CASE("BrushShape::Square paints the corner a circle of the same radius misses") {
    // `Square` was missing from the enum while three tool panels cast combo index 1 into it. The
    // Chebyshev branch it selects has always existed, so this pins the newly-named enumerator to the
    // behaviour that already shipped — and would fail loudly if someone "cleaned up" the else branch.
    //
    // Geometry: spacing 1, brush at texel (2,2), radius 1.2.
    //   corner (1,1): Chebyshev 1.0/1.2 = 0.83 -> inside the square
    //                 Euclidean 1.414/1.2 = 1.18 -> OUTSIDE the circle
    //   edge   (2,1): 1.0/1.2 = 0.83 in both -> the control that proves the brush ran at all
    auto makeParams = [](terrain::BrushShape shape)
    {
        terrain::WeightBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(2.0f, 2.0f);
        params.tileWorldOrigin = glm::vec2(0.0f, 0.0f);
        params.brushRadius = 1.2f;
        params.brushStrength = 1.0f;
        params.brushOpacity = 1.0f;
        params.vertexSpacing = 1.0f;
        params.verticesPerSide = 5;
        params.falloff = terrain::BrushFalloff::Constant; // exactly 1.0, so only geometry decides
        params.shape = shape;
        params.brushType = terrain::PaintBrushType::PaintLayer;
        params.activeLayer = 1;
        params.deltaTime = 1.0f;
        params.invert = false;
        return params;
    };

    terrain::TileWeightMapData circleMap;
    circleMap.initializeDefault(5);
    REQUIRE(terrain::WeightBrushApplicator::apply(circleMap, makeParams(terrain::BrushShape::Circle)));

    terrain::TileWeightMapData squareMap;
    squareMap.initializeDefault(5);
    REQUIRE(terrain::WeightBrushApplicator::apply(squareMap, makeParams(terrain::BrushShape::Square)));

    // Channel 1 holds palette layer 1 under the default palette.
    CHECK(circleMap.getWeight(1, 2, 1) > 0.0f);  // edge texel: both shapes reach it
    CHECK(squareMap.getWeight(1, 2, 1) > 0.0f);

    CHECK(circleMap.getWeight(1, 1, 1) == doctest::Approx(0.0f)); // corner: circle misses
    CHECK(squareMap.getWeight(1, 1, 1) > 0.0f);                   // corner: square reaches
}

TEST_CASE("a texel painted only with hidden layers packs to nothing") {
    // The documented consequence, pinned so it cannot regress into something subtler: with no weight
    // left the shader's clamp takes over and the texel composites black. The editor's last-visible-layer
    // guard is what keeps this from swallowing a whole terrain.
    terrain::TileWeightMapData wm;
    wm.initializeDefault(5);
    wm.setWeight(0, 0, 0, 0.0f);
    wm.setWeight(1, 0, 0, 1.0f);

    auto material = makeMaterial(2);
    material.layers[1].enabled = false;

    const auto masked = packWeights(wm, terrain::buildLayerEnabledMask(material));

    uint32_t total = 0;
    for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch) total += masked[ch];
    CHECK(total == 0u);
}

} // TEST_SUITE
