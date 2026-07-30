#include <doctest.h>

#include "data/TerrainStrokeUndoCommands.hpp"
#include "events/EventDispatcher.hpp"
#include "events/terrain/TerrainStrokeEvents.hpp"

#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"

#include <utility>
#include <vector>

// VK-1615: terrain sculpt / weight-paint / hole stroke undo.
//
// These test the undo commands in isolation, in the style of test_mesh_brush_undo.cpp:
// the restore event is faked on the real dispatcher so no TerrainService, TerrainGrid,
// Vulkan device or physics provider is needed. TerrainTile is constructed directly
// (precedent: test_terrain.cpp).

namespace
{
    using RestoreTiles = events::terrain::RestoreStrokeStateCommand;
    using RestoreMask = events::terrain::RestoreSurfaceMaskRegionCommand;
    using Kind = events::terrain::StrokeDataKind;

    struct TileHandlerGuard
    {
        ~TileHandlerGuard()
        {
            events::EventDispatcher::instance().unregisterCommandHandler<RestoreTiles>();
        }
    };

    struct MaskHandlerGuard
    {
        ~MaskHandlerGuard()
        {
            events::EventDispatcher::instance().unregisterCommandHandler<RestoreMask>();
        }
    };

    // Records every restore command the command under test dispatches, plus how many
    // times it was invoked (so "one event per stroke, not per tile" is checkable).
    struct RestoreLog
    {
        std::vector<RestoreTiles> commands;

        [[nodiscard]] const RestoreTiles& last() const { return commands.back(); }
        [[nodiscard]] size_t count() const { return commands.size(); }
    };

    TileHandlerGuard installTileHandler(RestoreLog& log)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<RestoreTiles>();
        dispatcher.registerCommandHandler<RestoreTiles>(
            [&log](const RestoreTiles& command)
            {
                log.commands.push_back(command);
            });
        return {};
    }

    MaskHandlerGuard installMaskHandler(std::vector<RestoreMask>& log)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<RestoreMask>();
        dispatcher.registerCommandHandler<RestoreMask>(
            [&log](const RestoreMask& command)
            {
                log.push_back(command);
            });
        return {};
    }

    terrain::TerrainTileConfig lowResConfig()
    {
        terrain::TerrainTileConfig config;
        config.resolution = terrain::TileResolution::Low; // 33 verts, 32 quads
        return config;
    }

    // A tile whose height/weight/hole arrays are sized correctly for its resolution and
    // filled with a recognisable constant, so a restore can be checked bit-exactly.
    terrain::TerrainTile makeTile(int32_t x, int32_t z, float height)
    {
        terrain::TerrainTile tile(terrain::TileCoord(x, z), lowResConfig());
        const uint32_t verts = tile.config.getVertexCount();
        tile.heightData.assign(static_cast<size_t>(verts) * verts, height);
        return tile;
    }

    std::vector<float> heightsOf(const terrain::TerrainTile& tile)
    {
        return tile.heightData;
    }

    terrain::TileWeightMapData makeWeights(uint32_t resolution, float channel0, uint8_t baseLayer)
    {
        terrain::TileWeightMapData weights;
        weights.initializeDefault(resolution);
        for (auto& texel : weights.layerWeights[0])
            texel = channel0;
        weights.layerIndices[0] = baseLayer;
        return weights;
    }
}

TEST_SUITE("TerrainStrokeUndo")
{
    TEST_CASE("terrain_stroke_undo: empty stroke has no changes")
    {
        services::TerrainStrokeUndoCommand command(7, "Sculpt Terrain");
        CHECK_FALSE(command.hasChanges());
        CHECK(command.getTileCount() == 0);
        CHECK(command.getDescription() == "Sculpt Terrain");
    }

    TEST_CASE("terrain_stroke_undo: an unchanged tile is pruned")
    {
        auto tile = makeTile(0, 0, 5.0f);

        services::TerrainStrokeUndoCommand command(1, "Sculpt Terrain");
        // "before" is byte-identical to the live tile: the brush reported this tile as
        // affected but nothing actually moved (a corner dab, or a seam already welded).
        const bool added = command.addTile(0, 0, events::terrain::strokeKindBit(Kind::Heights),
                                           heightsOf(tile), {}, {}, tile);

        CHECK_FALSE(added);
        CHECK_FALSE(command.hasChanges());
    }

    TEST_CASE("terrain_stroke_undo: a sculpt stroke carries heights only")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        auto tile = makeTile(2, 3, 9.0f);
        const auto before = heightsOf(tile);
        tile.heightData.assign(tile.heightData.size(), 12.0f); // the stroke raised it

        services::TerrainStrokeUndoCommand command(4, "Sculpt Terrain");
        REQUIRE(command.addTile(2, 3, events::terrain::strokeKindBit(Kind::Heights),
                                before, {}, {}, tile));

        command.undo();
        REQUIRE(log.count() == 1);
        REQUIRE(log.last().tiles.size() == 1);
        const auto& state = log.last().tiles[0];

        CHECK(events::terrain::hasStrokeKind(state.kinds, Kind::Heights));
        // A sculpt stroke must not claim -- and therefore must not clobber -- the tile's
        // weights or holes.
        CHECK_FALSE(events::terrain::hasStrokeKind(state.kinds, Kind::Weights));
        CHECK_FALSE(events::terrain::hasStrokeKind(state.kinds, Kind::Holes));
        CHECK(state.holeMask.empty());
        CHECK_FALSE(state.weightMap.isInitialized());
    }

    TEST_CASE("terrain_stroke_undo: undo restores pre-stroke heights, redo restores post-stroke")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        auto tile = makeTile(0, 0, 1.0f);
        const auto before = heightsOf(tile);
        tile.heightData.assign(tile.heightData.size(), 4.5f);
        const auto after = heightsOf(tile);

        services::TerrainStrokeUndoCommand command(11, "Sculpt Terrain");
        REQUIRE(command.addTile(0, 0, events::terrain::strokeKindBit(Kind::Heights),
                                before, {}, {}, tile));

        command.undo();
        REQUIRE(log.count() == 1);
        CHECK(log.last().entityId == 11);
        // Byte copies, so exact equality is the right assertion -- not Approx.
        CHECK(log.last().tiles[0].heightData == before);

        command.execute();
        REQUIRE(log.count() == 2);
        CHECK(log.last().tiles[0].heightData == after);

        // A second round trip must be identical: the command holds bytes, not a delta.
        command.undo();
        REQUIRE(log.count() == 3);
        CHECK(log.last().tiles[0].heightData == before);
    }

    TEST_CASE("terrain_stroke_undo: weight snapshot round-trips layerIndices")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        auto tile = makeTile(0, 0, 0.0f);
        const uint32_t resolution = tile.config.getVertexCount();

        const auto before = makeWeights(resolution, 1.0f, /*baseLayer=*/0);
        tile.weightMap = makeWeights(resolution, 0.25f, /*baseLayer=*/5);

        services::TerrainStrokeUndoCommand command(3, "Paint Terrain");
        REQUIRE(command.addTile(0, 0, events::terrain::strokeKindBit(Kind::Weights),
                                {}, before, {}, tile));

        command.undo();
        REQUIRE(log.count() == 1);
        const auto& state = log.last().tiles[0];
        REQUIRE(events::terrain::hasStrokeKind(state.kinds, Kind::Weights));
        CHECK(state.weightMap.resolution == resolution);
        CHECK(state.weightMap.layerIndices[0] == 0);
        CHECK(state.weightMap.layerWeights[0] == before.layerWeights[0]);
    }

    TEST_CASE("terrain_stroke_undo: a palette-only change is still detected")
    {
        // SetBaseLayer reinitialises the map and rewrites layerIndices[0]. If the channel
        // data happens to come back identical, only the palette differs -- a plain
        // per-channel compare would prune this stroke and lose the undo entry.
        auto tile = makeTile(0, 0, 0.0f);
        const uint32_t resolution = tile.config.getVertexCount();

        const auto before = makeWeights(resolution, 1.0f, /*baseLayer=*/0);
        tile.weightMap = makeWeights(resolution, 1.0f, /*baseLayer=*/6);

        services::TerrainStrokeUndoCommand command(3, "Paint Terrain");
        CHECK(command.addTile(0, 0, events::terrain::strokeKindBit(Kind::Weights),
                              {}, before, {}, tile));
    }

    TEST_CASE("terrain_stroke_undo: an empty pre-stroke hole mask restores as empty")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        auto tile = makeTile(1, 1, 0.0f);
        // Pre-stroke: this tile had never had a hole punched, so holeMask was empty.
        const std::vector<uint8_t> before;
        tile.initializeHoleMask();
        tile.setHole(4, 4, true);

        services::TerrainStrokeUndoCommand command(2, "Terrain Holes");
        REQUIRE(command.addTile(1, 1, events::terrain::strokeKindBit(Kind::Holes),
                                {}, {}, before, tile));

        command.undo();
        REQUIRE(log.count() == 1);
        const auto& state = log.last().tiles[0];

        // The kinds bit is what carries "restore holes"; the empty vector is the payload.
        // If emptiness meant "skip", undoing the very first hole on a tile would silently
        // do nothing.
        CHECK(events::terrain::hasStrokeKind(state.kinds, Kind::Holes));
        CHECK(state.holeMask.empty());

        command.execute();
        REQUIRE(log.count() == 2);
        CHECK(log.last().tiles[0].holeMask == tile.holeMask);
    }

    TEST_CASE("terrain_stroke_undo: a multi-tile stroke emits exactly one restore command")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        services::TerrainStrokeUndoCommand command(8, "Sculpt Terrain");

        for (int32_t i = 0; i < 3; ++i)
        {
            auto tile = makeTile(i, 0, 1.0f);
            const auto before = heightsOf(tile);
            tile.heightData.assign(tile.heightData.size(), 2.0f + static_cast<float>(i));
            REQUIRE(command.addTile(i, 0, events::terrain::strokeKindBit(Kind::Heights),
                                    before, {}, {}, tile));
        }
        REQUIRE(command.getTileCount() == 3);

        command.undo();
        // One event for the whole stroke -> one collider pass, one Ctrl+Z.
        CHECK(log.count() == 1);
        CHECK(log.last().tiles.size() == 3);
    }

    TEST_CASE("terrain_stroke_undo: a mixed-kind stroke keeps each tile's kinds separate")
    {
        RestoreLog log;
        auto guard = installTileHandler(log);

        services::TerrainStrokeUndoCommand command(9, "Terrain Holes");

        // Tile A: heights and holes both changed (a hole stroke that also welded a seam).
        auto tileA = makeTile(0, 0, 1.0f);
        const auto heightsBeforeA = heightsOf(tileA);
        tileA.heightData.assign(tileA.heightData.size(), 3.0f);
        tileA.initializeHoleMask();
        tileA.setHole(1, 1, true);
        REQUIRE(command.addTile(0, 0,
                                static_cast<uint8_t>(events::terrain::strokeKindBit(Kind::Heights)
                                                     | events::terrain::strokeKindBit(Kind::Holes)),
                                heightsBeforeA, {}, {}, tileA));

        // Tile B: only the hole mask changed; its heights were reported but untouched.
        auto tileB = makeTile(1, 0, 1.0f);
        const auto heightsBeforeB = heightsOf(tileB);
        tileB.initializeHoleMask();
        tileB.setHole(0, 2, true);
        REQUIRE(command.addTile(1, 0,
                                static_cast<uint8_t>(events::terrain::strokeKindBit(Kind::Heights)
                                                     | events::terrain::strokeKindBit(Kind::Holes)),
                                heightsBeforeB, {}, {}, tileB));

        command.undo();
        REQUIRE(log.count() == 1);
        REQUIRE(log.last().tiles.size() == 2);

        const auto& stateA = log.last().tiles[0];
        CHECK(events::terrain::hasStrokeKind(stateA.kinds, Kind::Heights));
        CHECK(events::terrain::hasStrokeKind(stateA.kinds, Kind::Holes));

        const auto& stateB = log.last().tiles[1];
        // Heights were requested but pruned, so a heights-only restore is not forced on a
        // tile the stroke never actually moved.
        CHECK_FALSE(events::terrain::hasStrokeKind(stateB.kinds, Kind::Heights));
        CHECK(events::terrain::hasStrokeKind(stateB.kinds, Kind::Holes));
    }

    TEST_CASE("terrain_stroke_undo: getMemoryFootprint scales with the snapshot")
    {
        services::TerrainStrokeUndoCommand small(1, "Sculpt Terrain");
        {
            auto tile = makeTile(0, 0, 1.0f);
            const auto before = heightsOf(tile);
            tile.heightData.assign(tile.heightData.size(), 2.0f);
            REQUIRE(small.addTile(0, 0, events::terrain::strokeKindBit(Kind::Heights),
                                  before, {}, {}, tile));
        }

        services::TerrainStrokeUndoCommand large(1, "Paint Terrain");
        {
            auto tile = makeTile(0, 0, 1.0f);
            const uint32_t resolution = tile.config.getVertexCount();
            const auto before = makeWeights(resolution, 1.0f, 0);
            tile.weightMap = makeWeights(resolution, 0.5f, 0);
            REQUIRE(large.addTile(0, 0, events::terrain::strokeKindBit(Kind::Weights),
                                  {}, before, {}, tile));
        }

        const size_t verts = 33u * 33u;
        // Heights: before + after floats, at minimum.
        CHECK(small.getMemoryFootprint() >= verts * sizeof(float) * 2);
        // Weights are 8 channels, so a paint snapshot dominates a sculpt one.
        CHECK(large.getMemoryFootprint() > small.getMemoryFootprint());

        services::TerrainStrokeUndoCommand empty(1, "Sculpt Terrain");
        CHECK(empty.getMemoryFootprint() == 0);
    }

    TEST_CASE("surface_mask_undo: a rect round-trips on one channel")
    {
        std::vector<RestoreMask> log;
        auto guard = installMaskHandler(log);

        const std::vector<uint8_t> before{10, 11, 12, 13};
        const std::vector<uint8_t> after{20, 21, 22, 23};

        services::SurfaceMaskStrokeUndoCommand command(
            /*channel=*/1, /*width=*/64, /*height=*/64,
            /*minX=*/4, /*minZ=*/8, /*rectW=*/2, /*rectH=*/2,
            before, after, "Paint Surface Mask");

        REQUIRE(command.hasChanges());

        command.undo();
        REQUIRE(log.size() == 1);
        CHECK(log[0].channel == 1);
        CHECK(log[0].maskWidth == 64);
        CHECK(log[0].maskHeight == 64);
        CHECK(log[0].minX == 4);
        CHECK(log[0].minZ == 8);
        CHECK(log[0].rectWidth == 2);
        CHECK(log[0].rectHeight == 2);
        CHECK(log[0].texels == before);

        command.execute();
        REQUIRE(log.size() == 2);
        CHECK(log[1].texels == after);
    }

    TEST_CASE("surface_mask_undo: an unchanged rect reports no changes")
    {
        const std::vector<uint8_t> same{1, 2, 3, 4};
        services::SurfaceMaskStrokeUndoCommand unchanged(
            0, 64, 64, 0, 0, 2, 2, same, same, "Paint Surface Mask");
        CHECK_FALSE(unchanged.hasChanges());

        services::SurfaceMaskStrokeUndoCommand empty(
            0, 64, 64, 0, 0, 0, 0, {}, {}, "Paint Surface Mask");
        CHECK_FALSE(empty.hasChanges());
        CHECK(empty.getMemoryFootprint() == 0);
    }
}
