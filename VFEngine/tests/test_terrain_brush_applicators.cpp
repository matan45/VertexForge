#include <doctest.h>

#include <terrain/HoleBrushApplicator.hpp>
#include <terrain/TerrainMaterialTypes.hpp>
#include <terrain/TerrainWeightMap.hpp>
#include <terrain/WeightBrushApplicator.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    terrain::WeightBrushApplicator::ApplyParams makeWeightParams()
    {
        terrain::WeightBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(1.0f, 1.0f);
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 0.75f;
        params.brushStrength = 0.25f;
        params.brushOpacity = 1.0f;
        params.vertexSpacing = 1.0f;
        params.verticesPerSide = 3;
        params.falloff = terrain::BrushFalloff::Constant;
        params.shape = terrain::BrushShape::Circle;
        params.brushType = terrain::PaintBrushType::PaintLayer;
        params.activeLayer = 1;
        params.deltaTime = 1.0f;
        params.invert = false;
        return params;
    }
}

TEST_SUITE("TerrainBrushApplicators")
{
    TEST_CASE("weight brush paints only affected texels and preserves normalization")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(3);
        auto params = makeWeightParams();

        REQUIRE(terrain::WeightBrushApplicator::apply(weights, params));
        CHECK(weights.getWeight(0, 1, 1) == 0.75f);
        CHECK(weights.getWeight(1, 1, 1) == 0.25f);
        CHECK(weights.getWeight(0, 0, 0) == 1.0f);
        CHECK(weights.getWeight(1, 0, 0) == 0.0f);

        float sum = 0.0f;
        for (uint32_t channel = 0; channel < terrain::WEIGHT_CHANNELS; ++channel)
            sum += weights.getWeight(channel, 1, 1);
        CHECK(sum == doctest::Approx(1.0f));
    }

    TEST_CASE("inverted paint erases into the hard-coded fallback channel")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(1);
        weights.setWeight(0, 0, 0, 0.2f);
        weights.setWeight(1, 0, 0, 0.8f);

        auto params = makeWeightParams();
        params.brushCenter = glm::vec2(0.0f);
        params.verticesPerSide = 1;
        params.activeLayer = 0;
        params.brushStrength = 0.1f;
        params.invert = true;

        REQUIRE(terrain::WeightBrushApplicator::apply(weights, params));
        CHECK(weights.getWeight(0, 0, 0) == doctest::Approx(0.1f));
        CHECK(weights.getWeight(1, 0, 0) == doctest::Approx(0.9f));
    }

    TEST_CASE("unsupported SetBaseLayer reports an affected texel without changing weights")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(1);
        const auto before = weights.layerWeights;

        auto params = makeWeightParams();
        params.brushCenter = glm::vec2(0.0f);
        params.verticesPerSide = 1;
        params.brushType = terrain::PaintBrushType::SetBaseLayer;

        CHECK(terrain::WeightBrushApplicator::apply(weights, params));
        CHECK(weights.layerWeights == before);
    }

    TEST_CASE("weight brush rejects unavailable storage and palette layers")
    {
        terrain::TileWeightMapData uninitialized;
        auto params = makeWeightParams();
        CHECK_FALSE(terrain::WeightBrushApplicator::apply(uninitialized, params));

        terrain::TileWeightMapData weights;
        weights.initializeDefault(3);
        params.activeLayer = terrain::MAX_TERRAIN_LAYERS;
        CHECK_FALSE(terrain::WeightBrushApplicator::apply(weights, params));
    }

    TEST_CASE("palette assignment uses the first empty channel and preserves an existing assignment")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(2);

        CHECK(weights.assignChannel(10) == 1);
        CHECK(weights.layerIndices[1] == 10);
        CHECK(weights.assignChannel(10) == 1);
    }

    TEST_CASE("palette eviction breaks equal-weight ties toward the lowest channel")
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(1);
        for (uint32_t channel = 0; channel < terrain::WEIGHT_CHANNELS; ++channel)
            weights.setWeight(channel, 0, 0, 0.125f);

        REQUIRE(weights.assignChannel(10) == 0);
        CHECK(weights.layerIndices[0] == 10);
        CHECK(weights.getWeight(0, 0, 0) == 0.0f);
        for (uint32_t channel = 1; channel < terrain::WEIGHT_CHANNELS; ++channel)
            CHECK(weights.getWeight(channel, 0, 0) == doctest::Approx(1.0f / 7.0f));
    }

    TEST_CASE("uninitialized palette assignment returns zero without changing the palette")
    {
        terrain::TileWeightMapData weights;
        const auto before = weights.layerIndices;

        CHECK(weights.assignChannel(10) == 0);
        CHECK(weights.layerIndices == before);
    }

    TEST_CASE("hole brush samples quad centers and repeated writes are no-ops")
    {
        std::vector<uint8_t> holes(1, 0);
        terrain::HoleBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(0.0f);
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 0.7f;
        params.vertexSpacing = 1.0f;
        params.quadsPerSide = 1;
        params.falloff = terrain::BrushFalloff::Constant;
        params.shape = terrain::BrushShape::Circle;
        params.erase = false;

        CHECK_FALSE(terrain::HoleBrushApplicator::apply(holes, params));
        CHECK(holes[0] == 0);

        params.brushRadius = 0.71f;
        REQUIRE(terrain::HoleBrushApplicator::apply(holes, params));
        CHECK(holes[0] == 1);
        CHECK_FALSE(terrain::HoleBrushApplicator::apply(holes, params));

        params.erase = true;
        REQUIRE(terrain::HoleBrushApplicator::apply(holes, params));
        CHECK(holes[0] == 0);
    }

    TEST_CASE("hole brush uses an inclusive binary half-influence threshold")
    {
        std::vector<uint8_t> holes(1, 0);
        terrain::HoleBrushApplicator::ApplyParams params{};
        params.brushCenter = glm::vec2(0.5f, 0.0f);
        params.tileWorldOrigin = glm::vec2(0.0f);
        params.brushRadius = 1.0f;
        params.vertexSpacing = 1.0f;
        params.quadsPerSide = 1;
        params.falloff = terrain::BrushFalloff::Linear;
        params.shape = terrain::BrushShape::Circle;
        params.erase = false;

        REQUIRE(terrain::HoleBrushApplicator::apply(holes, params));
        CHECK(holes[0] == 1);

        holes[0] = 0;
        params.brushRadius = 0.99f;
        CHECK_FALSE(terrain::HoleBrushApplicator::apply(holes, params));
        CHECK(holes[0] == 0);
    }
}
