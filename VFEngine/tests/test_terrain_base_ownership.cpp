#include <doctest.h>

#include <terrain/SplineCorridorDeform.hpp>
#include <terrain/TerrainGrid.hpp>
#include <terrain/TerrainHeightLayerStore.hpp>
#include <terrain/TerrainTile.hpp>

#include <memory>
#include <vector>

// VK-1645 -- authoritative base vs derived heights, at TerrainGrid level.
//
// Real TerrainGrid, no TerrainFileCache, no TerrainService, no Vulkan: recomposeDirtyDerived and
// normalizeDerivedSeams both tolerate a null file cache, and the grid is CPU-constructible
// (precedent: test_terrain_serializer_fixture.hpp).
//
// The sculpt brush itself is NOT exercised here -- it needs an ITerrainBrushComputeProvider and
// runs inside TerrainService. What these tests pin is the ownership model the brush routes into:
// an edit written to the base survives everything that can happen to the derived plane.

namespace
{
    constexpr float FLAT_HEIGHT = 4.0f;

    terrain::TerrainTileConfig lowResConfig()
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low; // 33 verts
        config.worldTileSize = 32.0f;
        return config;
    }

    std::unique_ptr<terrain::TerrainGrid> makeGrid(const std::vector<terrain::TileCoord>& coords)
    {
        auto grid = std::make_unique<terrain::TerrainGrid>(lowResConfig());
        grid->setHeightSampler([](float, float) { return FLAT_HEIGHT; });
        for (const terrain::TileCoord& coord : coords)
            grid->addTile(coord);
        return grid;
    }

    terrain::SplineCorridorParams corridorParams()
    {
        terrain::SplineCorridorParams params;
        params.corridorWidth = 4.0f;
        params.falloffWidth = 3.0f;
        return params;
    }

    // Straight spline down the middle of tile (0,0), pulling the corridor to y = 10.
    std::vector<glm::vec3> centreSpline()
    {
        return {
            glm::vec3(16.0f, 10.0f, 0.0f),
            glm::vec3(16.0f, 10.0f, 16.0f),
            glm::vec3(16.0f, 10.0f, 32.0f),
        };
    }

    // Mirrors what the ApplySplineDeformCommand handler does: seed a base per covered tile from
    // the CURRENT plane, then register the layer.
    void applyCorridorLayer(terrain::TerrainGrid& grid, uint64_t id,
                            const std::vector<terrain::TileCoord>& coords,
                            std::vector<glm::vec3> samples)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        terrain::HeightLayerRecord record;
        record.id = id;
        record.visible = true;

        const terrain::SplineCorridorParams params = corridorParams();
        record.eval = [samples, params](
            const terrain::TileCoord& c, const terrain::TerrainTileConfig& cfg,
            const std::vector<float>& in, std::vector<float>& out)
        {
            terrain::applySplineCorridorToTile(c, cfg, samples, params, in, out);
        };

        for (const terrain::TileCoord& coord : coords)
        {
            terrain::TerrainTile* tile = grid.getTile(coord);
            if (!tile)
                continue;
            store.adoptBase(coord, tile->heightData, tile->config.getVertexCount());
            record.affected.insert(coord);
        }

        store.addLayer(std::move(record));

        for (const terrain::TileCoord& coord : coords)
        {
            store.markDerivedStale(coord);
            for (uint8_t i = 0; i < 4; ++i)
            {
                store.markDerivedStale(
                    coord + terrain::TileCoord::getNeighborOffset(static_cast<terrain::TileEdge>(i)));
            }
        }

        grid.recomposeDirtyDerived(0);
    }

    void invalidate(terrain::TerrainGrid& grid, const terrain::TileCoord& coord)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();
        store.markDerivedStale(coord);
        for (uint8_t i = 0; i < 4; ++i)
        {
            store.markDerivedStale(
                coord + terrain::TileCoord::getNeighborOffset(static_cast<terrain::TileEdge>(i)));
        }
    }

    size_t vertexIndex(const terrain::TerrainTile& tile, uint32_t x, uint32_t z)
    {
        return static_cast<size_t>(z) * tile.config.getVertexCount() + x;
    }

    void checkPlanesEqual(const std::vector<float>& actual, const std::vector<float>& expected)
    {
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (actual[i] != expected[i])
            {
                CHECK(actual[i] == expected[i]); // reports the first mismatch with its index
                return;
            }
        }
        CHECK(true);
    }
}

TEST_SUITE("TerrainBaseOwnership")
{
    // The headline acceptance criterion: an ordinary sculpt under an active spline edits the
    // authoritative base, and therefore survives that spline's deletion. Before VK-1645 the
    // spline's whole-plane snapshot restore annihilated it.
    TEST_CASE("a sculpt under an active spline edits the base and survives the spline")
    {
        auto grid = makeGrid({{0, 0}});
        applyCorridorLayer(*grid, 1, {{0, 0}}, centreSpline());

        terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();
        terrain::TerrainTile* tile = grid->getTile({0, 0});
        REQUIRE(tile != nullptr);
        REQUIRE(store.isCovered({0, 0}));

        terrain::BaseHeightBlock* block = store.base({0, 0});
        REQUIRE(block != nullptr);

        // A corner well outside the corridor, and the corridor core itself.
        const size_t cornerIdx = vertexIndex(*tile, 0, 0);
        const size_t coreIdx = vertexIndex(*tile, 16, 16);

        SUBCASE("the corridor is in derived output but never in the base")
        {
            CHECK(block->heights[coreIdx] == FLAT_HEIGHT); // base is untouched ground
            CHECK(tile->heightData[coreIdx] == 10.0f);     // derived carries the corridor
        }

        // Sculpt: the brush writes the AUTHORITATIVE plane, which for a covered tile is the base.
        const float bump = 3.0f;
        block->heights[cornerIdx] += bump;
        block->dirty = true;
        invalidate(*grid, {0, 0});
        grid->recomposeDirtyDerived(0);

        SUBCASE("the dab lands in the base and shows through the composite")
        {
            CHECK(block->heights[cornerIdx] == FLAT_HEIGHT + bump);
            // Outside the corridor the layer contributes nothing, so derived == base there.
            CHECK(tile->heightData[cornerIdx] == FLAT_HEIGHT + bump);
            // The corridor core still wins, because corridorBlend is 1 there.
            CHECK(tile->heightData[coreIdx] == 10.0f);
        }

        SUBCASE("deleting the spline keeps the dab and drops the corridor")
        {
            invalidate(*grid, {0, 0});
            CHECK(store.removeLayer(1));
            grid->recomposeDirtyDerived(0);

            // This is the bug VK-1645 exists to kill. Before, deleting a spline restored a
            // snapshot of the ALREADY-COMPOSITED plane, which annihilated the dab. Now the
            // corridor was never in the base to begin with, so dropping the layer and recomposing
            // leaves exactly the artist's edit.
            REQUIRE(store.hasBase({0, 0}));
            CHECK(store.base({0, 0})->heights[cornerIdx] == FLAT_HEIGHT + bump);

            // Coverage is sticky: with an empty stack compose yields derived == base. Were it
            // to flip back to "derived is authoritative", the deleted corridor would stay baked
            // into heightData forever.
            CHECK(store.isCovered({0, 0}));
            CHECK(tile->heightData[coreIdx] == FLAT_HEIGHT);          // corridor gone
            CHECK(tile->heightData[cornerIdx] == FLAT_HEIGHT + bump); // dab preserved
        }

        SUBCASE("hiding the spline drops the corridor but keeps coverage")
        {
            invalidate(*grid, {0, 0});
            CHECK(store.setLayerVisible(1, false));
            grid->recomposeDirtyDerived(0);

            CHECK(store.isCovered({0, 0}));                          // still authoritative
            CHECK(tile->heightData[coreIdx] == FLAT_HEIGHT);         // corridor gone
            CHECK(tile->heightData[cornerIdx] == FLAT_HEIGHT + bump); // dab preserved
        }
    }

    TEST_CASE("repeated recompose is byte-stable")
    {
        auto grid = makeGrid({{0, 0}, {1, 0}});
        applyCorridorLayer(*grid, 1, {{0, 0}, {1, 0}}, centreSpline());

        terrain::TerrainTile* a = grid->getTile({0, 0});
        terrain::TerrainTile* b = grid->getTile({1, 0});
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);

        const std::vector<float> firstA = a->heightData;
        const std::vector<float> firstB = b->heightData;

        for (int cycle = 0; cycle < 10; ++cycle)
        {
            invalidate(*grid, {0, 0});
            invalidate(*grid, {1, 0});
            grid->recomposeDirtyDerived(0);

            checkPlanesEqual(a->heightData, firstA);
            checkPlanesEqual(b->heightData, firstB);
        }

        SUBCASE("and ten hide/show cycles land back on the same bytes")
        {
            terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();
            for (int cycle = 0; cycle < 10; ++cycle)
            {
                invalidate(*grid, {0, 0});
                invalidate(*grid, {1, 0});
                store.setLayerVisible(1, false);
                grid->recomposeDirtyDerived(0);

                invalidate(*grid, {0, 0});
                invalidate(*grid, {1, 0});
                store.setLayerVisible(1, true);
                grid->recomposeDirtyDerived(0);

                checkPlanesEqual(a->heightData, firstA);
                checkPlanesEqual(b->heightData, firstB);
            }
        }
    }

    // The seam rule that VK-1645 turns on its head. Averaging both sides would drag the uncovered
    // neighbour's AUTHORITATIVE edge halfway toward the covered tile on every recompose:
    // (c+u)/2, then 3c/4+u/4, then 7c/8+u/8 ... ten hide/show cycles and ground the user never
    // touched has been rewritten. One-sided conformance is the only rule that cannot do that.
    TEST_CASE("a covered-to-uncovered seam never writes the uncovered side")
    {
        auto grid = makeGrid({{0, 0}, {1, 0}});

        terrain::TerrainTile* covered = grid->getTile({0, 0});
        terrain::TerrainTile* uncovered = grid->getTile({1, 0});
        REQUIRE(covered != nullptr);
        REQUIRE(uncovered != nullptr);

        // Give the uncovered neighbour a distinctive edge so a stray write is unmistakable.
        const uint32_t verts = uncovered->config.getVertexCount();
        for (uint32_t z = 0; z < verts; ++z)
            uncovered->heightData[vertexIndex(*uncovered, 0, z)] = 42.0f;

        const std::vector<float> uncoveredSnapshot = uncovered->heightData;

        applyCorridorLayer(*grid, 1, {{0, 0}}, centreSpline());

        SUBCASE("ten recomposes leave the uncovered plane byte-identical")
        {
            for (int cycle = 0; cycle < 10; ++cycle)
            {
                invalidate(*grid, {0, 0});
                grid->recomposeDirtyDerived(0);
                checkPlanesEqual(uncovered->heightData, uncoveredSnapshot);
            }
        }

        SUBCASE("and the covered side conforms to it, so the seam still matches")
        {
            invalidate(*grid, {0, 0});
            grid->recomposeDirtyDerived(0);

            const uint32_t lastIdx = covered->config.getVertexCount() - 1;
            for (uint32_t z = 0; z < verts; ++z)
            {
                CHECK(covered->heightData[vertexIndex(*covered, lastIdx, z)]
                      == uncovered->heightData[vertexIndex(*uncovered, 0, z)]);
            }
        }
    }

    TEST_CASE("a covered-to-covered seam matches on both sides and stays stable")
    {
        auto grid = makeGrid({{0, 0}, {1, 0}});

        terrain::TerrainTile* a = grid->getTile({0, 0});
        terrain::TerrainTile* b = grid->getTile({1, 0});
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);

        // Distinct bases so the weld has something real to reconcile.
        const uint32_t verts = a->config.getVertexCount();
        for (uint32_t z = 0; z < verts; ++z)
        {
            a->heightData[vertexIndex(*a, verts - 1, z)] = 8.0f;
            b->heightData[vertexIndex(*b, 0, z)] = 2.0f;
        }

        applyCorridorLayer(*grid, 1, {{0, 0}, {1, 0}}, centreSpline());

        const std::vector<float> firstA = a->heightData;
        const std::vector<float> firstB = b->heightData;

        for (uint32_t z = 0; z < verts; ++z)
        {
            CHECK(a->heightData[vertexIndex(*a, verts - 1, z)]
                  == b->heightData[vertexIndex(*b, 0, z)]);
        }

        for (int cycle = 0; cycle < 5; ++cycle)
        {
            invalidate(*grid, {0, 0});
            invalidate(*grid, {1, 0});
            grid->recomposeDirtyDerived(0);

            checkPlanesEqual(a->heightData, firstA);
            checkPlanesEqual(b->heightData, firstB);
        }
    }

    TEST_CASE("uncovered coords take part in the seam pass but are never composed")
    {
        auto grid = makeGrid({{0, 0}, {1, 0}});
        applyCorridorLayer(*grid, 1, {{0, 0}}, centreSpline());

        terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();
        terrain::TerrainTile* uncovered = grid->getTile({1, 0});
        REQUIRE(uncovered != nullptr);

        // Marking an uncovered coord stale must clear the flag without composing -- it has no
        // base, so "composing" it could only overwrite authoritative data with nothing.
        store.markDerivedStale({1, 0});
        const std::vector<float> before = uncovered->heightData;

        const uint32_t composed = grid->recomposeDirtyDerived(0);

        CHECK(composed == 0); // no COVERED tile was stale
        CHECK_FALSE(store.isDerivedStale({1, 0}));
        checkPlanesEqual(uncovered->heightData, before);
    }

    TEST_CASE("the budget bounds the seeds and leaves the rest stale")
    {
        std::vector<terrain::TileCoord> coords;
        for (int32_t x = 0; x < 6; ++x)
            coords.push_back({x, 0});

        auto grid = makeGrid(coords);
        applyCorridorLayer(*grid, 1, coords, centreSpline());

        terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();
        for (const terrain::TileCoord& coord : coords)
            store.markDerivedStale(coord);

        REQUIRE(store.staleCount() == coords.size());

        // Two seeds, but their ring pulls in the covered neighbours as well -- recomposing both
        // sides of a seam in the same pass is what makes the weld byte-stable.
        const uint32_t composed = grid->recomposeDirtyDerived(2);
        CHECK(composed >= 2);
        CHECK(composed < coords.size());
        CHECK(store.staleCount() > 0); // the remainder waits for the next drain

        // Draining without a budget finishes the job.
        grid->recomposeDirtyDerived(0);
        CHECK(store.staleCount() == 0);
    }

    TEST_CASE("an unloaded tile keeps its base and its coverage")
    {
        auto grid = makeGrid({{0, 0}});
        applyCorridorLayer(*grid, 1, {{0, 0}}, centreSpline());

        terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();
        terrain::TerrainTile* tile = grid->getTile({0, 0});
        REQUIRE(tile != nullptr);

        // The artist's edit, in the authoritative plane.
        const size_t cornerIdx = vertexIndex(*tile, 0, 0);
        REQUIRE(store.base({0, 0}) != nullptr);
        store.base({0, 0})->heights[cornerIdx] = 77.0f;
        invalidate(*grid, {0, 0});
        grid->recomposeDirtyDerived(0);
        const std::vector<float> derivedBefore = tile->heightData;

        SUBCASE("stream-out destroys the tile but not the base")
        {
            // This is exactly what TerrainService::streamOutTile does.
            CHECK(grid->removeTile({0, 0}));
            CHECK(grid->getTile({0, 0}) == nullptr);

            CHECK(store.isCovered({0, 0}));
            REQUIRE(store.hasBase({0, 0}));
            CHECK(store.base({0, 0})->heights[cornerIdx] == 77.0f);
        }

        SUBCASE("a stale coord with no resident tile is skipped and stays stale")
        {
            CHECK(grid->removeTile({0, 0}));
            store.markDerivedStale({0, 0});

            CHECK(grid->recomposeDirtyDerived(0) == 0);
            CHECK(store.isDerivedStale({0, 0})); // stream-in must still recompose it
        }

        SUBCASE("stream-in re-attaches by coord and recomposes to the same bytes")
        {
            CHECK(grid->removeTile({0, 0}));

            // addTile re-creates the tile from the height sampler, i.e. flat ground with no
            // memory of the edit -- the recompose from the surviving base is what restores it.
            terrain::TerrainTile* reloaded = grid->addTile({0, 0});
            REQUIRE(reloaded != nullptr);
            CHECK(reloaded->heightData[cornerIdx] == FLAT_HEIGHT);

            invalidate(*grid, {0, 0});
            grid->recomposeDirtyDerived(0);

            checkPlanesEqual(reloaded->heightData, derivedBefore);
            CHECK(reloaded->heightData[cornerIdx] == 77.0f);
        }
    }

    TEST_CASE("recomposing marks the tile for mesh regeneration but never for the unbudgeted pass")
    {
        auto grid = makeGrid({{0, 0}, {1, 0}});
        applyCorridorLayer(*grid, 1, {{0, 0}}, centreSpline());

        terrain::TerrainTile* tile = grid->getTile({0, 0});
        REQUIRE(tile != nullptr);

        tile->isDirty = false;
        tile->dirtyLODMask = 0;
        tile->edgeSyncDirty = false;

        invalidate(*grid, {0, 0});
        grid->recomposeDirtyDerived(0);

        CHECK(tile->isDirty);
        CHECK(tile->dirtyLODMask == 0x3F);
        // edgeSyncDirty routes into the UNBUDGETED regeneration pass; a wide invalidation must
        // never land there or a 1024-tile recompose becomes a multi-second stall.
        CHECK_FALSE(tile->edgeSyncDirty);
    }
}
