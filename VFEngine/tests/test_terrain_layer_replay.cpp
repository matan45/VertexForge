#include <doctest.h>

#include <terrain/SplineCorridorDeform.hpp>
#include <terrain/TerrainGrid.hpp>
#include <terrain/TerrainHeightLayerStore.hpp>
#include <terrain/TerrainTile.hpp>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

// VK-1647 -- deterministic replay of the reserved height-layer stack.
//
// VK-1645 proved that composition restarts from the authoritative base; these tests pin the
// property that makes the stack EDITABLE: for a given ordered set of definitions the derived plane
// is the same bytes however that set was arrived at. Add, hide, show, reorder, edit and delete are
// all just different routes to the same ordered set.
//
// Same constraints as test_terrain_base_ownership.cpp: real TerrainGrid, null file cache, no
// TerrainService, no Vulkan. The helpers below mirror what the TerrainServiceHandlers commands do,
// for the same reason applyCorridorLayer does there -- the handlers themselves need an
// EventDispatcher, which is per-binary.
//
// Every comparison is bit-exact. A tolerance here would hide exactly the drift these tests exist
// to catch: the corridor evaluator is not idempotent in its falloff band, so a plane that was
// composed twice differs from one composed once by an amount an epsilon would swallow.

namespace
{
    constexpr float FLAT_HEIGHT = 4.0f;

    terrain::TerrainTileConfig lowResConfig()
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low; // 33 verts, 1.0 spacing at 32 m
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

    terrain::SplineCorridorParams corridorParams(float embankment = 0.0f)
    {
        terrain::SplineCorridorParams params;
        params.corridorWidth = 4.0f;
        params.falloffWidth = 3.0f;
        params.embankmentHeight = embankment;
        return params;
    }

    // Runs north-south through the middle of tile (0,0), pulling the corridor to y = 10.
    std::vector<glm::vec3> northSouthSpline()
    {
        return {
            glm::vec3(16.0f, 10.0f, 0.0f),
            glm::vec3(16.0f, 10.0f, 16.0f),
            glm::vec3(16.0f, 10.0f, 32.0f),
        };
    }

    // Runs east-west through the middle of tile (0,0) at a DIFFERENT target height, so the two
    // corridors disagree where they cross and stack order is observable.
    std::vector<glm::vec3> eastWestSpline()
    {
        return {
            glm::vec3(0.0f, 6.0f, 16.0f),
            glm::vec3(16.0f, 6.0f, 16.0f),
            glm::vec3(32.0f, 6.0f, 16.0f),
        };
    }

    // Spans tiles (0,0) and (1,0) only.
    std::vector<glm::vec3> westSpline()
    {
        return {
            glm::vec3(0.0f, 9.0f, 16.0f),
            glm::vec3(32.0f, 9.0f, 16.0f),
            glm::vec3(60.0f, 9.0f, 16.0f),
        };
    }

    // Spans tiles (1,0) and (2,0) only, so it shares exactly ONE tile with westSpline().
    std::vector<glm::vec3> eastSpline()
    {
        return {
            glm::vec3(36.0f, 5.0f, 16.0f),
            glm::vec3(64.0f, 5.0f, 16.0f),
            glm::vec3(92.0f, 5.0f, 16.0f),
        };
    }

    terrain::SplineCorridorLayerParams layerParams(std::vector<glm::vec3> samples,
                                                   float embankment = 0.0f)
    {
        terrain::SplineCorridorLayerParams params;
        params.corridor = corridorParams(embankment);
        params.samples = std::move(samples);
        return params;
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

    void invalidateAll(terrain::TerrainGrid& grid,
                       const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash>& coords)
    {
        for (const terrain::TileCoord& coord : coords)
            invalidate(grid, coord);
    }

    // Mirrors the ApplySplineDeformCommand handler's ADD branch: seed a base per covered tile from
    // the current plane, register the layer, invalidate, recompose.
    void applyLayer(terrain::TerrainGrid& grid, uint64_t id,
                    const std::vector<terrain::TileCoord>& coords,
                    terrain::SplineCorridorLayerParams params)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        terrain::HeightLayerRecord record;
        record.id = id;
        record.visible = true;
        record.type = terrain::HeightLayerType::SplineCorridor;
        record.spline = std::move(params);
        record.eval = terrain::makeSplineCorridorEval(record.spline);

        for (const terrain::TileCoord& coord : coords)
        {
            terrain::TerrainTile* tile = grid.getTile(coord);
            if (!tile)
                continue;
            store.adoptBase(coord, tile->heightData, tile->config.getVertexCount());
            record.affected.insert(coord);
        }

        REQUIRE(store.addLayer(std::move(record)));

        for (const terrain::TileCoord& coord : coords)
            invalidate(grid, coord);

        grid.recomposeDirtyDerived(0);
    }

    // Mirrors the handler's EDIT branch, `coords` standing in for the geometric candidate list the
    // handler gets from splineCorridorAffectedTiles: seed bases for whatever is resident, carry
    // over previously-claimed coords the new geometry still reaches, update in place, invalidate
    // OLD and new, recompose.
    //
    // Mirroring rather than calling the handler is the same compromise applyCorridorLayer makes in
    // test_terrain_base_ownership.cpp -- the handler needs an EventDispatcher, which is per-binary.
    // So this pins the RULE; keep the two in step by hand.
    void editLayer(terrain::TerrainGrid& grid, uint64_t id,
                   const std::vector<terrain::TileCoord>& coords,
                   terrain::SplineCorridorLayerParams params)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        const terrain::HeightLayerRecord* existing = store.layer(id);
        REQUIRE(existing != nullptr);
        const auto previousAffected = existing->affected;

        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> affected;
        for (const terrain::TileCoord& coord : coords)
        {
            terrain::TerrainTile* tile = grid.getTile(coord);
            if (!tile)
                continue;
            store.adoptBase(coord, tile->heightData, tile->config.getVertexCount());
            affected.insert(coord);
        }

        // updateLayer REPLACES the set, so an unloaded tile the new geometry still covers has to be
        // carried across explicitly or the corridor is un-claimed there and lost on stream-in.
        const std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> reachable(
            coords.begin(), coords.end());
        for (const terrain::TileCoord& coord : previousAffected)
        {
            if (reachable.find(coord) != reachable.end() && !grid.getTile(coord))
                affected.insert(coord);
        }

        REQUIRE(store.updateLayer(id, std::move(params), affected));
        REQUIRE(store.setLayerVisible(id, true));

        invalidateAll(grid, previousAffected);
        invalidateAll(grid, affected);

        grid.recomposeDirtyDerived(0);
    }

    // Mirrors the MoveHeightLayerCommand handler: impact set first (afterwards the bracket no
    // longer names the crossed layers), then the move, then recompose.
    void moveLayer(terrain::TerrainGrid& grid, uint64_t id, size_t newIndex)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        const auto from = store.layerIndex(id);
        REQUIRE(from.has_value());

        invalidateAll(grid, store.reorderImpactSet(id, std::min(*from, newIndex),
                                                   std::max(*from, newIndex)));
        REQUIRE(store.moveLayer(id, newIndex));

        grid.recomposeDirtyDerived(0);
    }

    void removeLayer(terrain::TerrainGrid& grid, uint64_t id)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        // Marked while the record still exists; afterwards nothing can say which tiles it covered.
        const terrain::HeightLayerRecord* record = store.layer(id);
        REQUIRE(record != nullptr);
        invalidateAll(grid, record->affected);

        REQUIRE(store.removeLayer(id));
        grid.recomposeDirtyDerived(0);
    }

    void setVisible(terrain::TerrainGrid& grid, uint64_t id, bool visible)
    {
        terrain::TerrainHeightLayerStore& store = grid.getHeightLayers();

        const terrain::HeightLayerRecord* record = store.layer(id);
        REQUIRE(record != nullptr);
        invalidateAll(grid, record->affected);

        REQUIRE(store.setLayerVisible(id, visible));
        grid.recomposeDirtyDerived(0);
    }

    std::vector<float> planeOf(terrain::TerrainGrid& grid, const terrain::TileCoord& coord)
    {
        const terrain::TerrainTile* tile = grid.getTile(coord);
        REQUIRE(tile != nullptr);
        return tile->heightData;
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

    bool planesEqual(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }
}

TEST_SUITE("TerrainLayerReplay")
{
    // Two corridors crossing on one tile. Deleting either must leave exactly what the other alone
    // would have produced -- not "the plane as it was before the second was applied", which is what
    // a per-spline snapshot restores and why it broke when splines overlapped.
    TEST_CASE("overlapping splines replay independently of application order")
    {
        const terrain::TileCoord centre{0, 0};
        const std::vector<terrain::TileCoord> coords{centre};

        // Reference planes: each spline applied alone to a fresh grid.
        std::vector<float> nsAlone;
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            nsAlone = planeOf(*grid, centre);
        }

        std::vector<float> ewAlone;
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));
            ewAlone = planeOf(*grid, centre);
        }

        // They must actually disagree somewhere, or every assertion below passes vacuously.
        REQUIRE_FALSE(planesEqual(nsAlone, ewAlone));

        SUBCASE("both applied, then the FIRST deleted -- out of application order")
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

            removeLayer(*grid, 1);
            checkPlanesEqual(planeOf(*grid, centre), ewAlone);
        }

        SUBCASE("both applied, then the SECOND deleted")
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

            removeLayer(*grid, 2);
            checkPlanesEqual(planeOf(*grid, centre), nsAlone);
        }

        SUBCASE("deleting both leaves the untouched base")
        {
            auto grid = makeGrid(coords);
            const std::vector<float> original = planeOf(*grid, centre);

            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

            removeLayer(*grid, 2);
            removeLayer(*grid, 1);

            // Coverage is sticky, so the tile still owns its base -- and with an empty stack
            // compose yields derived == base, which is exactly the ground before any spline.
            checkPlanesEqual(planeOf(*grid, centre), original);
        }

        SUBCASE("hiding a layer matches deleting it")
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

            setVisible(*grid, 1, false);
            checkPlanesEqual(planeOf(*grid, centre), ewAlone);

            // ...and showing it again restores the two-layer result bit-for-bit.
            setVisible(*grid, 1, true);

            auto reference = makeGrid(coords);
            applyLayer(*reference, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*reference, 2, coords, layerParams(eastWestSpline()));
            checkPlanesEqual(planeOf(*grid, centre), planeOf(*reference, centre));
        }
    }

    // Reorder is the operation VK-1647 exists for. It must produce the same bytes as applying the
    // same definitions in that order from scratch, and it must be reversible.
    TEST_CASE("reorder produces the same result as applying in that order")
    {
        const terrain::TileCoord centre{0, 0};
        const std::vector<terrain::TileCoord> coords{centre};

        // Ground truth: layer 2 applied first, then layer 1.
        std::vector<float> ewThenNs;
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            ewThenNs = planeOf(*grid, centre);
        }

        auto grid = makeGrid(coords);
        applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
        applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));
        const std::vector<float> nsThenEw = planeOf(*grid, centre);

        // Order has to matter, or the test proves nothing.
        REQUIRE_FALSE(planesEqual(nsThenEw, ewThenNs));

        SUBCASE("moving the first layer to the back matches applying it last")
        {
            moveLayer(*grid, 1, 1);
            REQUIRE(grid->getHeightLayers().layerIndex(1).value() == 1);
            checkPlanesEqual(planeOf(*grid, centre), ewThenNs);
        }

        SUBCASE("moving there and back is bit-identical to never having moved")
        {
            moveLayer(*grid, 1, 1);
            moveLayer(*grid, 1, 0);

            REQUIRE(grid->getHeightLayers().layerIndex(1).value() == 0);
            checkPlanesEqual(planeOf(*grid, centre), nsThenEw);
        }

        SUBCASE("a no-op move changes nothing")
        {
            const std::vector<float> before = planeOf(*grid, centre);
            REQUIRE(grid->getHeightLayers().moveLayer(1, 0));
            grid->recomposeDirtyDerived(0);
            checkPlanesEqual(planeOf(*grid, centre), before);
        }
    }

    // The "sparse" half of the invalidation AC: a reorder must not touch tiles whose composition
    // cannot have changed, because on a real map that is the entire length of both roads.
    TEST_CASE("reorder invalidates exactly the tiles the moved layer shares with those it crossed")
    {
        const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}, {2, 0}};
        auto grid = makeGrid(coords);

        const std::vector<terrain::TileCoord> westTiles{{0, 0}, {1, 0}};
        const std::vector<terrain::TileCoord> eastTiles{{1, 0}, {2, 0}};

        applyLayer(*grid, 1, westTiles, layerParams(westSpline()));
        applyLayer(*grid, 2, eastTiles, layerParams(eastSpline()));

        terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();

        SUBCASE("the impact set is the intersection, not either whole set")
        {
            const auto impact = store.reorderImpactSet(1, 0, 1);

            CHECK(impact.size() == 1);
            CHECK(impact.count(terrain::TileCoord{1, 0}) == 1);
            CHECK(impact.count(terrain::TileCoord{0, 0}) == 0); // west only -- order cannot matter
            CHECK(impact.count(terrain::TileCoord{2, 0}) == 0); // east only -- likewise
        }

        SUBCASE("a tile only the moved layer claims is bit-unchanged by the move")
        {
            const std::vector<float> westOnlyBefore = planeOf(*grid, {0, 0});
            const std::vector<float> eastOnlyBefore = planeOf(*grid, {2, 0});

            moveLayer(*grid, 1, 1);

            checkPlanesEqual(planeOf(*grid, {0, 0}), westOnlyBefore);
            checkPlanesEqual(planeOf(*grid, {2, 0}), eastOnlyBefore);
        }

        SUBCASE("an unknown id or an out-of-range bracket yields an empty set")
        {
            CHECK(store.reorderImpactSet(99, 0, 1).empty());
            CHECK(store.reorderImpactSet(1, 5, 9).empty());
        }
    }

    // Editing replaces a layer's contribution instead of stacking a second one. Before VK-1647
    // "Regenerate Road" registered a new layer every time and carved the terrain once per attempt.
    TEST_CASE("editing a layer in place replaces its contribution")
    {
        const terrain::TileCoord centre{0, 0};
        const std::vector<terrain::TileCoord> coords{centre};

        SUBCASE("an edit matches applying the new definition from scratch")
        {
            auto reference = makeGrid(coords);
            applyLayer(*reference, 1, coords, layerParams(eastWestSpline()));

            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            editLayer(*grid, 1, coords, layerParams(eastWestSpline()));

            CHECK(grid->getHeightLayers().layers().size() == 1); // not two
            checkPlanesEqual(planeOf(*grid, centre), planeOf(*reference, centre));
        }

        SUBCASE("editing back is bit-identical to never having edited")
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            const std::vector<float> original = planeOf(*grid, centre);

            editLayer(*grid, 1, coords, layerParams(eastWestSpline()));
            editLayer(*grid, 1, coords, layerParams(northSouthSpline()));

            checkPlanesEqual(planeOf(*grid, centre), original);
        }

        SUBCASE("an edit keeps the layer's stack position")
        {
            auto grid = makeGrid(coords);
            applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

            editLayer(*grid, 1, coords, layerParams(northSouthSpline(), 2.0f));

            // Still first. Re-authoring a road must not float its corridor to the top of the stack,
            // or every layer applied over it since would start composing underneath it.
            CHECK(grid->getHeightLayers().layerIndex(1).value() == 0);
            CHECK(grid->getHeightLayers().layerIndex(2).value() == 1);
        }

        SUBCASE("widening onto a new tile adopts that tile's CLEAN ground as its base")
        {
            const std::vector<terrain::TileCoord> both{{0, 0}, {1, 0}};
            auto grid = makeGrid(both);

            const std::vector<float> untouchedNeighbour = planeOf(*grid, {1, 0});

            // Applied over tile (0,0) only, so (1,0) owns no base yet.
            applyLayer(*grid, 1, {{0, 0}}, layerParams(westSpline()));
            REQUIRE_FALSE(grid->getHeightLayers().hasBase(terrain::TileCoord{1, 0}));

            editLayer(*grid, 1, both, layerParams(westSpline()));

            // The base adopted for the widened tile must be the ground as it was, not a plane
            // already carrying somebody's corridor. A layer only ever claims a tile it adopted a
            // base for, so an unclaimed tile's heightData is clean by construction -- this pins it.
            const terrain::BaseHeightBlock* adopted =
                grid->getHeightLayers().base(terrain::TileCoord{1, 0});
            REQUIRE(adopted != nullptr);
            checkPlanesEqual(adopted->heights, untouchedNeighbour);
        }

        // The regression the update branch introduced: updateLayer replaces `affected` wholesale,
        // so a set built by filtering on residency un-claims every unloaded part of the corridor.
        // Walk to one end of a long road, hit Regenerate, and the far half is erased from the
        // terrain as the camera returns -- lazily, and with coverage still sticky so nothing
        // announces it.
        SUBCASE("re-authoring keeps the corridor over tiles that are streamed out")
        {
            const std::vector<terrain::TileCoord> both{{0, 0}, {1, 0}};
            auto grid = makeGrid(both);

            applyLayer(*grid, 1, both, layerParams(westSpline()));
            const std::vector<float> farBefore = planeOf(*grid, {1, 0});

            // Stream (1,0) out. Its base survives, as stream-out must never drop artist data.
            REQUIRE(grid->removeTile({1, 0}));
            REQUIRE(grid->getHeightLayers().hasBase(terrain::TileCoord{1, 0}));

            // Re-author with the SAME geometry. The seeding loop can only reach (0,0), so a naive
            // update would ship affected = {(0,0)} and drop the far tile.
            editLayer(*grid, 1, both, layerParams(westSpline()));

            const terrain::HeightLayerRecord* layer = grid->getHeightLayers().layer(1);
            REQUIRE(layer != nullptr);
            CHECK(layer->affected.count(terrain::TileCoord{1, 0}) == 1);

            // Stream it back in and recompose: the corridor must still be there.
            REQUIRE(grid->addTile({1, 0}) != nullptr);
            invalidate(*grid, {1, 0});
            grid->recomposeDirtyDerived(0);
            checkPlanesEqual(planeOf(*grid, {1, 0}), farBefore);
        }

        SUBCASE("narrowing off a tile recomposes it back to base plus the remaining stack")
        {
            const std::vector<terrain::TileCoord> both{{0, 0}, {1, 0}};
            auto grid = makeGrid(both);

            const std::vector<float> untouchedNeighbour = planeOf(*grid, {1, 0});

            applyLayer(*grid, 1, both, layerParams(westSpline()));
            REQUIRE_FALSE(planesEqual(planeOf(*grid, {1, 0}), untouchedNeighbour));

            editLayer(*grid, 1, {{0, 0}}, layerParams(westSpline()));

            // Sticky coverage keeps (1,0)'s base, and with no layer claiming it any more compose
            // yields derived == base. Getting the OLD affected set invalidated is the whole point:
            // miss it and the narrowed-off corridor stays baked in forever.
            checkPlanesEqual(planeOf(*grid, {1, 0}), untouchedNeighbour);
        }
    }

    // Byte stability is the property every other test rests on. It is not obvious: the corridor
    // evaluator is not idempotent in its falloff band, and the seam pass writes a shared corner
    // twice per call.
    TEST_CASE("repeated evaluation is byte-stable")
    {
        const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        auto grid = makeGrid(coords);

        applyLayer(*grid, 1, coords, layerParams(northSouthSpline()));
        applyLayer(*grid, 2, coords, layerParams(eastWestSpline()));

        const std::vector<float> settled = planeOf(*grid, {0, 0});

        SUBCASE("ten bare recomposes change nothing")
        {
            for (int i = 0; i < 10; ++i)
            {
                for (const terrain::TileCoord& coord : coords)
                    invalidate(*grid, coord);
                grid->recomposeDirtyDerived(0);
            }

            checkPlanesEqual(planeOf(*grid, {0, 0}), settled);
        }

        SUBCASE("ten hide/show cycles change nothing")
        {
            for (int i = 0; i < 10; ++i)
            {
                setVisible(*grid, 1, false);
                setVisible(*grid, 1, true);
            }

            checkPlanesEqual(planeOf(*grid, {0, 0}), settled);
        }

        SUBCASE("ten reorder round trips change nothing")
        {
            for (int i = 0; i < 10; ++i)
            {
                moveLayer(*grid, 1, 1);
                moveLayer(*grid, 1, 0);
            }

            checkPlanesEqual(planeOf(*grid, {0, 0}), settled);
        }

        SUBCASE("ten edit round trips change nothing")
        {
            for (int i = 0; i < 10; ++i)
            {
                editLayer(*grid, 1, coords, layerParams(eastWestSpline()));
                editLayer(*grid, 1, coords, layerParams(northSouthSpline()));
            }

            checkPlanesEqual(planeOf(*grid, {0, 0}), settled);
        }
    }

    // Seams and the 4-way corner, across the full operation set rather than just recompose.
    TEST_CASE("corners and seams survive the whole operation set")
    {
        // A 2x2 covered block with an uncovered tile on the +X side of the bottom row, so the suite
        // exercises covered<->covered AND the one-sided covered<->uncovered rule.
        const std::vector<terrain::TileCoord> all{{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 0}};
        const std::vector<terrain::TileCoord> covered{{0, 0}, {1, 0}, {0, 1}, {1, 1}};

        auto grid = makeGrid(all);
        const std::vector<float> uncoveredBefore = planeOf(*grid, {2, 0});

        applyLayer(*grid, 1, covered, layerParams(northSouthSpline()));
        applyLayer(*grid, 2, covered, layerParams(eastWestSpline()));

        SUBCASE("the uncovered neighbour's authoritative plane is never written")
        {
            checkPlanesEqual(planeOf(*grid, {2, 0}), uncoveredBefore);

            // Every operation, then check again: one-sided welding means the covered side conforms
            // and the uncovered side is ground truth. Averaging instead would drag it halfway on
            // every single recompose.
            setVisible(*grid, 1, false);
            setVisible(*grid, 1, true);
            moveLayer(*grid, 1, 1);
            editLayer(*grid, 2, covered, layerParams(eastWestSpline(), 1.0f));
            removeLayer(*grid, 1);

            checkPlanesEqual(planeOf(*grid, {2, 0}), uncoveredBefore);
        }

        SUBCASE("the shared 4-way corner is stable across repeats")
        {
            const terrain::TerrainTile* tile = grid->getTile({0, 0});
            REQUIRE(tile != nullptr);
            const uint32_t last = tile->config.getVertexCount() - 1;
            const size_t cornerIndex = static_cast<size_t>(last) * tile->config.getVertexCount() + last;

            const float corner = planeOf(*grid, {0, 0})[cornerIndex];

            for (int i = 0; i < 10; ++i)
            {
                for (const terrain::TileCoord& coord : covered)
                    invalidate(*grid, coord);
                grid->recomposeDirtyDerived(0);
            }

            CHECK(planeOf(*grid, {0, 0})[cornerIndex] == corner);
        }

        SUBCASE("both sides of a covered<->covered seam agree")
        {
            const terrain::TerrainTile* left = grid->getTile({0, 0});
            const terrain::TerrainTile* right = grid->getTile({1, 0});
            REQUIRE(left != nullptr);
            REQUIRE(right != nullptr);

            const uint32_t verts = left->config.getVertexCount();
            for (uint32_t z = 0; z < verts; ++z)
            {
                const float mine = left->heightData[static_cast<size_t>(z) * verts + (verts - 1)];
                const float theirs = right->heightData[static_cast<size_t>(z) * verts];
                if (mine != theirs)
                {
                    CHECK(mine == theirs); // first disagreeing row only
                    return;
                }
            }
            CHECK(true);
        }
    }

    // Streamed-out tiles. The regression here is subtle and was live before VK-1647: a covered
    // coord with no resident tile used to take a budget slot and then be skipped, and because the
    // stale flag is never cleared for one, the SAME coords were re-picked every frame forever.
    TEST_CASE("streamed tiles neither starve the budget nor lose their base")
    {
        const std::vector<terrain::TileCoord> coords{{0, 0}, {1, 0}};
        auto grid = makeGrid(coords);

        applyLayer(*grid, 1, coords, layerParams(westSpline()));

        const std::vector<float> composedFar = planeOf(*grid, {1, 0});

        SUBCASE("a non-resident covered seed does not consume budget")
        {
            // Stream out (0,0). removeTile deliberately keeps its base block, so the coord stays
            // covered -- and it sorts FIRST under staleSorted()'s (z, x) ordering, which is what
            // made it able to shadow every resident tile behind it.
            REQUIRE(grid->removeTile({0, 0}));
            REQUIRE(grid->getHeightLayers().hasBase(terrain::TileCoord{0, 0}));

            grid->getHeightLayers().markDerivedStale({0, 0});
            grid->getHeightLayers().markDerivedStale({1, 0});

            // A budget of one. The non-resident coord must be stepped over rather than spent, so
            // the resident tile behind it still gets composed.
            CHECK(grid->recomposeDirtyDerived(1) == 1);

            // ...and its flag is kept, so streaming it back in still recomposes it.
            CHECK(grid->getHeightLayers().isDerivedStale({0, 0}));
        }

        SUBCASE("stream out and back restores the exact plane")
        {
            const std::vector<float> before = planeOf(*grid, {0, 0});

            REQUIRE(grid->removeTile({0, 0}));
            REQUIRE(grid->getTile({0, 0}) == nullptr);

            // addTileFromFile is the streaming path and needs a file cache; addTile is the
            // equivalent for a grid with none, and re-marking stale is what stream-in does.
            REQUIRE(grid->addTile({0, 0}) != nullptr);
            CHECK(grid->getHeightLayers().isCovered(terrain::TileCoord{0, 0}));

            invalidate(*grid, {0, 0});
            grid->recomposeDirtyDerived(0);

            checkPlanesEqual(planeOf(*grid, {0, 0}), before);
        }

        SUBCASE("progress counts resident and unloaded work separately")
        {
            REQUIRE(grid->removeTile({0, 0}));

            grid->getHeightLayers().markDerivedStale({0, 0});
            grid->getHeightLayers().markDerivedStale({1, 0});

            const auto status = grid->heightLayerRecomposeStatus();
            CHECK(status.pendingResident == 1);
            CHECK(status.pendingUnloaded == 1);

            // A tile waiting on the streamer must not be reported as recompose work, or a progress
            // bar would sit short of 100% for as long as the camera stays away.
            grid->recomposeDirtyDerived(0);
            const auto drained = grid->heightLayerRecomposeStatus();
            CHECK(drained.pendingResident == 0);
            CHECK(drained.pendingUnloaded == 1);
        }

        SUBCASE("a far tile that was never streamed out is unaffected")
        {
            checkPlanesEqual(planeOf(*grid, {1, 0}), composedFar);
        }

        // Pins WHY the progress fraction excludes pendingUnloaded, so nobody folds it back in.
        //
        // TerrainService::removeTile erases the base but leaves the coord in every layer's
        // `affected` (TerrainStreamingOps.cpp:147), so the coord still reads as covered. Anything
        // that later walks that layer's affected set -- invalidateHeightLayerTiles does, on every
        // hide/show/delete -- parks it in pendingUnloaded, and nothing will ever drain it: there is
        // no tile to compose and no base to compose from.
        SUBCASE("a deleted-then-claimed coord parks in pendingUnloaded forever")
        {
            terrain::TerrainHeightLayerStore& store = grid->getHeightLayers();

            // Exactly what TerrainService::removeTile does, in order.
            store.eraseBase({0, 0});
            REQUIRE(grid->removeTile({0, 0}));

            // Still claimed by layer 1, so still covered -- but baseless and non-resident.
            CHECK(store.isCovered(terrain::TileCoord{0, 0}));
            CHECK_FALSE(store.hasBase(terrain::TileCoord{0, 0}));

            store.markDerivedStale({0, 0});

            CHECK(grid->heightLayerRecomposeStatus().pendingUnloaded == 1);

            // Draining cannot clear it, which is the whole point: folded into the fraction it would
            // pin a progress bar below 100% for the rest of the session.
            grid->recomposeDirtyDerived(0);
            CHECK(grid->heightLayerRecomposeStatus().pendingUnloaded == 1);
            CHECK(grid->heightLayerRecomposeStatus().pendingResident == 0);
        }
    }
}
