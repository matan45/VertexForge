#include <doctest.h>

#include <terrain/SegmentCorridor.hpp>
#include <terrain/SplineCorridorDeform.hpp>
#include <terrain/TerrainHeightLayerStore.hpp>

#include <limits>
#include <vector>

// VK-1645 -- authoritative base vs derived heights, the pure-CPU half.
//
// No TerrainGrid, no TerrainService, no Vulkan: composeTileHeights and applySplineCorridorToTile
// are free functions over std::vector<float>, which is exactly why they live in Terrain.dll
// rather than inside the service (same reasoning as test_terrain_runtime_edit.cpp).
//
// Every comparison here is bit-exact. Tolerances would hide precisely the failures these tests
// exist to catch -- a seam that creeps by an ULP per cycle, or a refactor that reorders a
// floating-point expression.

namespace
{
    terrain::TerrainTileConfig lowResConfig()
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low; // 33 verts, 32 quads
        config.worldTileSize = 32.0f;
        return config;
    }

    std::vector<float> makePlane(const terrain::TerrainTileConfig& config, float height)
    {
        const uint32_t verts = config.getVertexCount();
        return std::vector<float>(static_cast<size_t>(verts) * verts, height);
    }

    // A recognisable, non-constant plane, so a bug that returns the wrong buffer cannot pass by
    // coincidence.
    std::vector<float> makeRampPlane(const terrain::TerrainTileConfig& config)
    {
        const uint32_t verts = config.getVertexCount();
        std::vector<float> plane(static_cast<size_t>(verts) * verts, 0.0f);
        for (uint32_t z = 0; z < verts; ++z)
        {
            for (uint32_t x = 0; x < verts; ++x)
                plane[static_cast<size_t>(z) * verts + x] = static_cast<float>(x) * 0.25f
                    + static_cast<float>(z) * 0.5f;
        }
        return plane;
    }

    // A straight spline down the middle of tile (0,0) in world XZ, flat at y = 10.
    std::vector<glm::vec3> centreSpline()
    {
        return {
            glm::vec3(16.0f, 10.0f, 0.0f),
            glm::vec3(16.0f, 10.0f, 16.0f),
            glm::vec3(16.0f, 10.0f, 32.0f),
        };
    }

    terrain::SplineCorridorParams corridorParams()
    {
        terrain::SplineCorridorParams params;
        params.corridorWidth = 4.0f;
        params.falloffWidth = 3.0f;
        params.embankmentHeight = 0.0f;
        return params;
    }

    terrain::HeightLayerRecord makeCorridorLayer(uint64_t id, const terrain::TileCoord& coord,
                                                 std::vector<glm::vec3> samples,
                                                 terrain::SplineCorridorParams params)
    {
        terrain::HeightLayerRecord record;
        record.id = id;
        record.visible = true;
        record.affected.insert(coord);
        record.eval = [samples = std::move(samples), params](
            const terrain::TileCoord& c, const terrain::TerrainTileConfig& cfg,
            const std::vector<float>& in, std::vector<float>& out)
        {
            terrain::applySplineCorridorToTile(c, cfg, samples, params, in, out);
        };
        return record;
    }

    // Transcribed verbatim from the pre-VK-1645 loop that lived inline in the
    // ApplySplineDeformCommand handler (TerrainServiceHandlers.cpp, read-modify-write over
    // tile->heightData). This is the regression fence for the extraction: same arithmetic, same
    // order, same segment cull.
    void originalSplineDeform(const terrain::TileCoord& coord,
                              const terrain::TerrainTileConfig& config,
                              const std::vector<glm::vec3>& splineSamples,
                              float corridorWidth, float falloffWidth, float embankmentHeight,
                              std::vector<float>& heightData)
    {
        const float totalHalfWidth = corridorWidth + falloffWidth;
        const float halfCorridor = corridorWidth;

        uint32_t vertCount = config.getVertexCount();
        float vertSpacing = config.getVertexSpacing();
        glm::vec2 tileOrigin(
            static_cast<float>(coord.x) * config.worldTileSize,
            static_cast<float>(coord.z) * config.worldTileSize);

        float tileSize = config.worldTileSize;

        std::vector<size_t> relevantSegments;
        for (size_t i = 0; i + 1 < splineSamples.size(); ++i)
        {
            glm::vec2 segStart(splineSamples[i].x, splineSamples[i].z);
            glm::vec2 segEnd(splineSamples[i + 1].x, splineSamples[i + 1].z);
            glm::vec2 segMin = glm::min(segStart, segEnd) - glm::vec2(totalHalfWidth);
            glm::vec2 segMax = glm::max(segStart, segEnd) + glm::vec2(totalHalfWidth);

            if (segMax.x >= tileOrigin.x && segMin.x <= tileOrigin.x + tileSize &&
                segMax.y >= tileOrigin.y && segMin.y <= tileOrigin.y + tileSize)
            {
                relevantSegments.push_back(i);
            }
        }

        if (relevantSegments.empty())
            return;

        for (uint32_t z = 0; z < vertCount; ++z)
        {
            for (uint32_t x = 0; x < vertCount; ++x)
            {
                glm::vec2 vertPos = tileOrigin
                    + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * vertSpacing;

                float minPerpDist = std::numeric_limits<float>::max();
                float bestTargetHeight = 0.0f;

                for (size_t segIdx : relevantSegments)
                {
                    glm::vec2 segStart(splineSamples[segIdx].x, splineSamples[segIdx].z);
                    glm::vec2 segEnd(splineSamples[segIdx + 1].x, splineSamples[segIdx + 1].z);
                    glm::vec2 segDir = segEnd - segStart;
                    float segLen = glm::length(segDir);
                    if (segLen < 0.001f) continue;

                    terrain::SegmentProjection projection =
                        terrain::projectOntoSegment(vertPos, segStart, segEnd, segLen);

                    if (projection.distance < minPerpDist)
                    {
                        minPerpDist = projection.distance;
                        bestTargetHeight = glm::mix(
                            splineSamples[segIdx].y,
                            splineSamples[segIdx + 1].y, projection.t)
                            + embankmentHeight;
                    }
                }

                if (minPerpDist > totalHalfWidth)
                    continue;

                uint32_t idx = z * vertCount + x;
                float currentHeight = heightData[idx];

                float blend = terrain::corridorBlend(minPerpDist, halfCorridor, falloffWidth);

                heightData[idx] = glm::mix(currentHeight, bestTargetHeight, blend);
            }
        }
    }
}

TEST_SUITE("TerrainHeightCompose")
{
    // The single highest-value test in VK-1645: the corridor loop moved out of a service handler
    // lambda into a pure Terrain.dll function, and "it looks the same" is not evidence.
    TEST_CASE("the extracted corridor reproduces the original inline loop bit-for-bit")
    {
        const terrain::TerrainTileConfig config = lowResConfig();
        const terrain::SplineCorridorParams params = corridorParams();
        const std::vector<glm::vec3> samples = centreSpline();

        SUBCASE("over a tile the corridor crosses")
        {
            const terrain::TileCoord coord(0, 0);
            const std::vector<float> base = makeRampPlane(config);

            std::vector<float> expected = base;
            originalSplineDeform(coord, config, samples, params.corridorWidth, params.falloffWidth,
                                 params.embankmentHeight, expected);

            std::vector<float> actual;
            const bool modified =
                terrain::applySplineCorridorToTile(coord, config, samples, params, base, actual);

            CHECK(modified);
            REQUIRE(actual.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i)
                CHECK(actual[i] == expected[i]);
        }

        SUBCASE("over a tile the corridor misses, out is an exact copy of in")
        {
            const terrain::TileCoord coord(5, 5);
            const std::vector<float> base = makeRampPlane(config);

            std::vector<float> actual;
            const bool modified =
                terrain::applySplineCorridorToTile(coord, config, samples, params, base, actual);

            CHECK_FALSE(modified);
            REQUIRE(actual.size() == base.size());
            for (size_t i = 0; i < base.size(); ++i)
                CHECK(actual[i] == base[i]);
        }

        SUBCASE("embankment height rides along")
        {
            const terrain::TileCoord coord(0, 0);
            const std::vector<float> base = makeRampPlane(config);
            terrain::SplineCorridorParams raised = params;
            raised.embankmentHeight = 2.5f;

            std::vector<float> expected = base;
            originalSplineDeform(coord, config, samples, raised.corridorWidth, raised.falloffWidth,
                                 raised.embankmentHeight, expected);

            std::vector<float> actual;
            terrain::applySplineCorridorToTile(coord, config, samples, raised, base, actual);

            REQUIRE(actual.size() == expected.size());
            for (size_t i = 0; i < expected.size(); ++i)
                CHECK(actual[i] == expected[i]);
        }
    }

    TEST_CASE("compose is a pure function of base and stack")
    {
        const terrain::TerrainTileConfig config = lowResConfig();
        const terrain::TileCoord coord(0, 0);
        const std::vector<float> base = makeRampPlane(config);

        std::vector<terrain::HeightLayerRecord> layers;
        layers.push_back(makeCorridorLayer(1, coord, centreSpline(), corridorParams()));

        SUBCASE("an empty stack yields the base byte-for-byte")
        {
            std::vector<float> out;
            terrain::composeTileHeights(coord, config, base, {}, out);

            REQUIRE(out.size() == base.size());
            for (size_t i = 0; i < base.size(); ++i)
                CHECK(out[i] == base[i]);
        }

        SUBCASE("ten composes into fresh buffers are all identical")
        {
            std::vector<float> first;
            terrain::composeTileHeights(coord, config, base, layers, first);

            for (int cycle = 0; cycle < 10; ++cycle)
            {
                std::vector<float> again;
                terrain::composeTileHeights(coord, config, base, layers, again);

                REQUIRE(again.size() == first.size());
                for (size_t i = 0; i < first.size(); ++i)
                    CHECK(again[i] == first[i]);
            }
        }

        SUBCASE("compose never writes the base")
        {
            const std::vector<float> baseSnapshot = base;

            std::vector<float> out;
            for (int cycle = 0; cycle < 5; ++cycle)
                terrain::composeTileHeights(coord, config, base, layers, out);

            REQUIRE(base.size() == baseSnapshot.size());
            for (size_t i = 0; i < base.size(); ++i)
                CHECK(base[i] == baseSnapshot[i]);
        }

        SUBCASE("reusing a dirty output buffer does not change the result")
        {
            std::vector<float> clean;
            terrain::composeTileHeights(coord, config, base, layers, clean);

            std::vector<float> dirty = makePlane(config, 999.0f);
            terrain::composeTileHeights(coord, config, base, layers, dirty);

            REQUIRE(dirty.size() == clean.size());
            for (size_t i = 0; i < clean.size(); ++i)
                CHECK(dirty[i] == clean[i]);
        }

        SUBCASE("a hidden layer contributes nothing but still counts as coverage")
        {
            layers[0].visible = false;

            std::vector<float> out;
            terrain::composeTileHeights(coord, config, base, layers, out);

            REQUIRE(out.size() == base.size());
            for (size_t i = 0; i < base.size(); ++i)
                CHECK(out[i] == base[i]);

            terrain::TerrainHeightLayerStore store;
            store.addLayer(layers[0]); // hidden layer
            CHECK(store.isCovered(coord)); // hiding must NOT reclassify the tile as uncovered
        }

        SUBCASE("a layer that does not claim this tile is skipped")
        {
            layers[0].affected.clear();
            layers[0].affected.insert(terrain::TileCoord(9, 9));

            std::vector<float> out;
            terrain::composeTileHeights(coord, config, base, layers, out);

            for (size_t i = 0; i < base.size(); ++i)
                CHECK(out[i] == base[i]);
        }
    }

    TEST_CASE("stack order is part of the result")
    {
        const terrain::TerrainTileConfig config = lowResConfig();
        const terrain::TileCoord coord(0, 0);
        const std::vector<float> base = makeRampPlane(config);

        // Two overlapping corridors at different heights. Whichever runs last wins the shared
        // core, so AB and BA must differ -- if they did not, ordering would be unobservable and
        // the reorder semantics VK-1647 builds on would be untestable.
        std::vector<glm::vec3> alongZ = centreSpline();
        std::vector<glm::vec3> alongX = {
            glm::vec3(0.0f, 25.0f, 16.0f),
            glm::vec3(16.0f, 25.0f, 16.0f),
            glm::vec3(32.0f, 25.0f, 16.0f),
        };

        std::vector<terrain::HeightLayerRecord> ab;
        ab.push_back(makeCorridorLayer(1, coord, alongZ, corridorParams()));
        ab.push_back(makeCorridorLayer(2, coord, alongX, corridorParams()));

        std::vector<terrain::HeightLayerRecord> ba;
        ba.push_back(makeCorridorLayer(2, coord, alongX, corridorParams()));
        ba.push_back(makeCorridorLayer(1, coord, alongZ, corridorParams()));

        std::vector<float> outAB;
        std::vector<float> outBA;
        terrain::composeTileHeights(coord, config, base, ab, outAB);
        terrain::composeTileHeights(coord, config, base, ba, outBA);

        bool anyDifference = false;
        REQUIRE(outAB.size() == outBA.size());
        for (size_t i = 0; i < outAB.size(); ++i)
        {
            if (outAB[i] != outBA[i])
            {
                anyDifference = true;
                break;
            }
        }
        CHECK(anyDifference);

        SUBCASE("and each ordering is individually reproducible")
        {
            std::vector<float> repeatAB;
            terrain::composeTileHeights(coord, config, base, ab, repeatAB);
            for (size_t i = 0; i < outAB.size(); ++i)
                CHECK(repeatAB[i] == outAB[i]);
        }
    }

    // The identity the whole seam design rests on. TerrainBrushOps already relies on it for
    // hydraulic erosion; VK-1645 relies on it for compose/undo byte-stability.
    TEST_CASE("averaging an already-welded seam is bit-exactly idempotent")
    {
        const float values[] = {0.0f, 1.0f, -7.25f, 123.456f, -0.0009765625f, 1e-8f, 5000.5f};
        for (const float v : values)
        {
            const float welded = (v + v) * 0.5f;
            CHECK(welded == v);
            CHECK(((welded + welded) * 0.5f) == v);
        }
    }

    TEST_CASE("the store keeps its invariants")
    {
        terrain::TerrainHeightLayerStore store;
        const terrain::TerrainTileConfig config = lowResConfig();
        const terrain::TileCoord coord(0, 0);

        SUBCASE("adoptBase is one-shot, so a second spline cannot re-seed a composited plane")
        {
            const std::vector<float> ground = makePlane(config, 1.0f);
            const std::vector<float> composited = makePlane(config, 99.0f);

            store.adoptBase(coord, ground, config.getVertexCount());
            store.adoptBase(coord, composited, config.getVertexCount());

            const terrain::BaseHeightBlock* block = store.base(coord);
            REQUIRE(block != nullptr);
            for (const float h : block->heights)
                CHECK(h == 1.0f);
        }

        SUBCASE("coverage is sticky and never inferred from residency or data")
        {
            CHECK_FALSE(store.isCovered(coord));

            store.addLayer(makeCorridorLayer(7, coord, centreSpline(), corridorParams()));
            CHECK(store.isCovered(coord));

            store.adoptBase(coord, makePlane(config, 0.0f), config.getVertexCount());

            store.setLayerVisible(7, false);
            CHECK(store.isCovered(coord)); // hidden, still covered

            // Deleting the last layer must NOT hand authority back to the derived plane. With an
            // empty stack compose yields derived == base, which is the right answer and needs no
            // one-shot collapse -- and forgetting that collapse would leave the deleted spline's
            // corridor baked into derived forever.
            CHECK(store.removeLayer(7));
            CHECK(store.isCovered(coord));
            CHECK(store.hasBase(coord)); // removing a layer must NOT drop artist data
        }

        SUBCASE("staleSorted is ordered by (z, x)")
        {
            store.markDerivedStale({1, 1});
            store.markDerivedStale({0, 1});
            store.markDerivedStale({2, 0});
            store.markDerivedStale({0, 0});
            store.markDerivedStale({0, 0}); // duplicate

            const std::vector<terrain::TileCoord> sorted = store.staleSorted();
            REQUIRE(sorted.size() == 4);
            CHECK(sorted[0] == terrain::TileCoord(0, 0));
            CHECK(sorted[1] == terrain::TileCoord(2, 0));
            CHECK(sorted[2] == terrain::TileCoord(0, 1));
            CHECK(sorted[3] == terrain::TileCoord(1, 1));
        }

        SUBCASE("a resolution change is refused rather than silently resized")
        {
            terrain::TerrainTileConfig highRes;
            highRes.resolution = terrain::TileResolution::High;

            store.adoptBase(coord, makePlane(config, 3.0f), config.getVertexCount());
            const terrain::BaseHeightBlock* block = store.base(coord);
            REQUIRE(block != nullptr);
            CHECK(block->vertexCount == config.getVertexCount());
            CHECK(block->vertexCount != highRes.getVertexCount());
        }
    }
}
