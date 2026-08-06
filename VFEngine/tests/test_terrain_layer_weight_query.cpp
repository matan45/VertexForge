#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "impl/scene/TerrainService.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TileHeightSampler.hpp"
#include <scene/SceneGraphSystem.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

static_assert(std::is_standard_layout_v<terrain::TerrainLayerWeight>);
static_assert(std::is_trivially_copyable_v<terrain::TerrainLayerWeight>);
static_assert(std::is_standard_layout_v<terrain::TerrainLayerWeightsAtResult>);
static_assert(std::is_trivially_copyable_v<terrain::TerrainLayerWeightsAtResult>);

namespace
{
    float legacySample(const terrain::TileWeightMapData& wm, uint8_t paletteLayer,
                       float localX, float localZ, float vertexSpacing)
    {
        const uint8_t channel = wm.findChannel(paletteLayer);
        if (channel == 0xFF || wm.resolution == 0)
            return 0.0f;
        const uint32_t res = wm.resolution;
        const float maxIdx = static_cast<float>(res - 1u);
        const float gx = std::clamp(localX / vertexSpacing, 0.0f, maxIdx);
        const float gz = std::clamp(localZ / vertexSpacing, 0.0f, maxIdx);
        const uint32_t x0 = static_cast<uint32_t>(gx);
        const uint32_t z0 = static_cast<uint32_t>(gz);
        const uint32_t x1 = std::min(x0 + 1u, res - 1u);
        const uint32_t z1 = std::min(z0 + 1u, res - 1u);
        const float fx = gx - static_cast<float>(x0);
        const float fz = gz - static_cast<float>(z0);
        const float w00 = wm.getWeight(channel, x0, z0);
        const float w10 = wm.getWeight(channel, x1, z0);
        const float w01 = wm.getWeight(channel, x0, z1);
        const float w11 = wm.getWeight(channel, x1, z1);
        const float wx0 = w00 + (w10 - w00) * fx;
        const float wx1 = w01 + (w11 - w01) * fx;
        return wx0 + (wx1 - wx0) * fz;
    }

    float findWeight(const terrain::TerrainLayerWeightsAtResult& result, uint8_t layer)
    {
        for (uint8_t i = 0; i < result.count; ++i)
            if (result.layers[i].layer == layer)
                return result.layers[i].weight;
        return 0.0f;
    }

    void fillChannel(terrain::TileWeightMapData& wm, uint8_t channel, float weight)
    {
        std::fill(wm.layerWeights[channel].begin(), wm.layerWeights[channel].end(), weight);
    }
}

TEST_SUITE("TerrainLayerWeightQuery")
{
    TEST_CASE("plural and memoized layer sampling preserve the legacy arithmetic exactly")
    {
        terrain::TileWeightMapData wm;
        wm.initializeDefault(5);
        wm.layerIndices = {5, 1, 9, 3, 12, 0, 31, 7};

        std::mt19937 rng(0x1622u);
        std::uniform_real_distribution<float> weights(0.0f, 1.0f);
        for (auto& channel : wm.layerWeights)
            for (float& weight : channel)
                weight = weights(rng);
        wm.normalizeAll();

        constexpr std::array<glm::vec2, 8> positions = {{
            {-2.0f, -1.0f}, {0.0f, 0.0f}, {0.25f, 0.75f}, {1.5f, 2.25f},
            {3.9f, 3.1f}, {4.0f, 4.0f}, {8.0f, 1.0f}, {2.5f, 9.0f}
        }};

        terrain::TileLayerWeightMemo memo(wm, 1.0f);
        for (const glm::vec2 position : positions)
        {
            terrain::TerrainLayerWeightsAtResult plural;
            REQUIRE(terrain::sampleTileLayerWeightsBilinear(
                wm, position.x, position.y, 1.0f, plural));

            float sum = 0.0f;
            for (uint8_t i = 0; i < plural.count; ++i)
                sum += plural.layers[i].weight;
            CHECK(sum == doctest::Approx(1.0f).epsilon(1e-5));

            for (uint8_t layer = 0; layer < 32; ++layer)
            {
                const float expected = legacySample(wm, layer, position.x, position.y, 1.0f);
                CHECK(terrain::sampleTileLayerWeightBilinear(
                    wm, layer, position.x, position.y, 1.0f) == expected);
                CHECK(findWeight(plural, layer) == expected);
                CHECK(memo.sample(layer, position.x, position.y) == expected);
                CHECK(memo.sample(layer, position.x, position.y) == expected);
            }
        }
    }

    TEST_CASE("plural sampling handles midpoint clamp resolution-one and stale output")
    {
        terrain::TileWeightMapData wm;
        wm.initializeDefault(2);
        fillChannel(wm, 0, 0.0f);
        wm.layerWeights[0][3] = 1.0f;

        terrain::TerrainLayerWeightsAtResult result;
        REQUIRE(terrain::sampleTileLayerWeightsBilinear(wm, 0.5f, 0.5f, 1.0f, result));
        CHECK(findWeight(result, 0) == 0.25f);

        REQUIRE(terrain::sampleTileLayerWeightsBilinear(wm, -5.0f, -3.0f, 1.0f, result));
        CHECK(findWeight(result, 0) == 0.0f);
        REQUIRE(terrain::sampleTileLayerWeightsBilinear(wm, 8.0f, 9.0f, 1.0f, result));
        CHECK(findWeight(result, 0) == 1.0f);

        terrain::TileWeightMapData one;
        one.initializeDefault(1);
        one.layerIndices[0] = 13;
        REQUIRE(terrain::sampleTileLayerWeightsBilinear(one, 50.0f, -50.0f, 1.0f, result));
        CHECK(findWeight(result, 13) == 1.0f);

        terrain::TileWeightMapData empty;
        result.valid = true;
        result.count = 3;
        result.layers[0] = {7, 0.75f};
        CHECK_FALSE(terrain::sampleTileLayerWeightsBilinear(empty, 0.0f, 0.0f, 1.0f, result));
        CHECK_FALSE(result.valid);
        CHECK(result.count == 0);
        for (const auto& layer : result.layers)
        {
            CHECK(layer.layer == 0);
            CHECK(layer.weight == 0.0f);
        }
    }

    TEST_CASE("plural sampling resolves malformed duplicate palette indices first-wins")
    {
        terrain::TileWeightMapData wm;
        wm.initializeDefault(2);
        wm.layerIndices = {5, 5, 2, 3, 4, 20, 6, 7};
        fillChannel(wm, 0, 0.25f);
        fillChannel(wm, 1, 0.75f);

        terrain::TerrainLayerWeightsAtResult result;
        REQUIRE(terrain::sampleTileLayerWeightsBilinear(wm, 0.5f, 0.5f, 1.0f, result));
        CHECK(result.count == terrain::WEIGHT_CHANNELS - 1);
        CHECK(findWeight(result, 5) == 0.25f);
        CHECK(terrain::sampleTileLayerWeightBilinear(wm, 5, 0.5f, 0.5f, 1.0f) == 0.25f);
    }

    TEST_CASE("compaction matches the renderer epsilon and preserves stable ties")
    {
        terrain::TerrainLayerWeightsAtResult result;
        result.valid = true;
        result.count = 5;
        result.layers[0] = {1, 0.5f};
        result.layers[1] = {2, terrain::TERRAIN_LAYER_WEIGHT_EPSILON};
        result.layers[2] = {3, 0.75f};
        result.layers[3] = {4, 0.5f};
        result.layers[4] = {5, terrain::TERRAIN_LAYER_WEIGHT_EPSILON - 0.0001f};

        terrain::compactLayerWeights(result, terrain::TERRAIN_LAYER_WEIGHT_EPSILON);

        REQUIRE(result.valid);
        REQUIRE(result.count == 4);
        CHECK(result.layers[0].layer == 3);
        CHECK(result.layers[1].layer == 1);
        CHECK(result.layers[2].layer == 4);
        CHECK(result.layers[3].layer == 2);
        CHECK(result.layers[3].weight == terrain::TERRAIN_LAYER_WEIGHT_EPSILON);
        for (uint8_t i = result.count; i < terrain::TERRAIN_LAYER_WEIGHT_SLOTS; ++i)
        {
            CHECK(result.layers[i].layer == 0);
            CHECK(result.layers[i].weight == 0.0f);
        }

        result = {};
        result.valid = true;
        result.count = 1;
        result.layers[0] = {9, 0.0005f};
        terrain::compactLayerWeights(result, terrain::TERRAIN_LAYER_WEIGHT_EPSILON);
        CHECK(result.valid);
        CHECK(result.count == 0);
    }

    TEST_CASE("production TerrainService handlers return aligned single and batch results")
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.clear();
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();

        events::terrain::GetTerrainLayerWeightsAtQuery singleQuery;
        {
            services::TerrainService service(sceneGraph);
            service.registerEventHandlers();

            services::TerrainCreationData creation;
            creation.tilesX = 1;
            creation.tilesZ = 1;
            creation.resolution = 0;
            creation.worldTileSize = 32.0f;
            const services::EntityHandle terrainEntity = service.createTerrain(creation);
            REQUIRE(terrainEntity.isValid());

            const auto tiles = service.getAllLoadedTiles();
            REQUIRE(tiles.size() == 1);
            REQUIRE(tiles[0] != nullptr);
            auto& wm = tiles[0]->weightMap;
            wm.layerIndices = {5, 2, 8, 3, 4, 0, 6, 7};
            fillChannel(wm, 0, 0.6f);
            fillChannel(wm, 1, 0.3f);
            fillChannel(wm, 2, terrain::TERRAIN_LAYER_WEIGHT_EPSILON);
            fillChannel(wm, 3, 0.0005f);
            for (uint8_t channel = 4; channel < terrain::WEIGHT_CHANNELS; ++channel)
                fillChannel(wm, channel, 0.0f);

            singleQuery.worldX = 1.0f;
            singleQuery.worldZ = 1.0f;
            const auto single = dispatcher.query(singleQuery);
            REQUIRE(single.valid);
            REQUIRE(single.count == 3);
            CHECK(single.layers[0].layer == 5);
            CHECK(single.layers[0].weight == 0.6f);
            CHECK(single.layers[1].layer == 2);
            CHECK(single.layers[1].weight == 0.3f);
            CHECK(single.layers[2].layer == 8);
            CHECK(single.layers[2].weight == terrain::TERRAIN_LAYER_WEIGHT_EPSILON);

            events::terrain::GetTerrainLayerWeightsBatchQuery batchQuery;
            batchQuery.positions = {
                {1.0f, 1.0f},
                {1000.0f, 1000.0f},
                {std::numeric_limits<float>::quiet_NaN(), 0.0f},
                {2.0f, 2.0f}
            };
            const auto batch = dispatcher.query(batchQuery);
            REQUIRE(batch.size() == batchQuery.positions.size());
            CHECK(batch[0].valid);
            CHECK_FALSE(batch[1].valid);
            CHECK_FALSE(batch[2].valid);
            CHECK(batch[3].valid);
            CHECK(batch[0].count == single.count);
            for (uint8_t i = 0; i < single.count; ++i)
            {
                CHECK(batch[0].layers[i].layer == single.layers[i].layer);
                CHECK(batch[0].layers[i].weight == single.layers[i].weight);
            }

            batchQuery.positions.clear();
            CHECK(dispatcher.query(batchQuery).empty());

            wm = {};
            CHECK_FALSE(dispatcher.query(singleQuery).valid);
            batchQuery.positions = {{1.0f, 1.0f}};
            const auto missingWeights = dispatcher.query(batchQuery);
            REQUIRE(missingWeights.size() == 1);
            CHECK_FALSE(missingWeights[0].valid);

            CHECK(service.deleteTerrain(terrainEntity));
        }

        CHECK_THROWS_AS(dispatcher.query(singleQuery), std::runtime_error);
    }
}
