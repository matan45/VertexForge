// VK-1648 — the height-layer stack operations end to end, through the REAL handlers.
//
// A live services::TerrainService over a real TerrainGrid, driven entirely through the dispatcher —
// which is what the Editor panel does, and therefore what proves the panel needs nothing else. No
// Vulkan is involved: createTerrain builds CPU tiles, and recomposeDirtyDerived is pure CPU
// (precedent: test_terrain_layer_weight_query.cpp).
//
// Every height comparison is bit-exact, and covers WHOLE planes including their boundary columns.
// Composition always restarts from the authoritative base, so a tolerance here would hide exactly
// the drift these tests exist to catch.
//
// Whole-plane equality is only safe because a terrain created with no heightmap is flat at 0.0
// (TerrainCreationOps.cpp): every base agrees at every shared column, so normalizeDerivedSeams is
// numerically the identity. Do NOT read these as proof that a seam column is invariant in general —
// it is not. A covered tile's boundary column is averaged with its covered neighbour's, so when the
// neighbour's composite legitimately changes, this tile's shared column moves with it even though
// its own contributor list did not. On a fixture with disagreeing bases these assertions would have
// to exclude the seam (test_terrain_layer_replay.cpp::checkPlanesEqualIgnoringColumn).

#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/terrain/HeightLayerUndoEvents.hpp"
#include "events/terrain/SplineTerrainEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/terrain/SplineTerrainServiceImpl.hpp"
#include <data/TerrainLayerUndoCommands.hpp>
#include <data/UndoTypes.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <terrain/TerrainGrid.hpp>
#include <terrain/TerrainTile.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    // Captures whatever the recording handlers push, without claiming the undo service's slots.
    // test_group_transform.cpp owns the real Undo/Redo/Push/Batch handlers via a function-local
    // static; this only borrows Push (and the two batch markers, which would otherwise throw) and
    // gives them back.
    struct UndoLog
    {
        std::vector<std::shared_ptr<services::IUndoableCommand>> pushed;

        [[nodiscard]] bool empty() const { return pushed.empty(); }
        [[nodiscard]] size_t size() const { return pushed.size(); }
        [[nodiscard]] services::IUndoableCommand& last() const { return *pushed.back(); }
    };

    class UndoCaptureGuard
    {
    public:
        explicit UndoCaptureGuard(UndoLog& log)
        {
            auto& d = events::EventDispatcher::instance();

            d.unregisterCommandHandler<events::undoredo::PushUndoableCommand>();
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [&log](const events::undoredo::PushUndoableCommand& cmd)
                {
                    if (cmd.command)
                        log.pushed.push_back(cmd.command);
                });

            d.unregisterCommandHandler<events::undoredo::BeginBatchCommand>();
            d.registerCommandHandler<events::undoredo::BeginBatchCommand>(
                [](const events::undoredo::BeginBatchCommand&) {});

            d.unregisterCommandHandler<events::undoredo::EndBatchCommand>();
            d.registerCommandHandler<events::undoredo::EndBatchCommand>(
                [](const events::undoredo::EndBatchCommand&) {});
        }

        ~UndoCaptureGuard()
        {
            auto& d = events::EventDispatcher::instance();
            d.unregisterCommandHandler<events::undoredo::PushUndoableCommand>();
            d.unregisterCommandHandler<events::undoredo::BeginBatchCommand>();
            d.unregisterCommandHandler<events::undoredo::EndBatchCommand>();
        }

        UndoCaptureGuard(const UndoCaptureGuard&) = delete;
        UndoCaptureGuard& operator=(const UndoCaptureGuard&) = delete;
    };

    // Destroys the terrain at the end of each run, BEFORE the service that owns its grid.
    //
    // Load-bearing, not politeness. createTerrain puts a terrain entity AND one entity per tile
    // into the process-global EntityRegistry (TerrainCreationOps.cpp), and ~TerrainService only
    // unregisters handlers — it does not remove them. doctest re-executes a TEST_CASE body once
    // per SUBCASE, so without this every iteration left another terrain behind whose components
    // referenced a grid the previous iteration had already freed. That is what crashed here: a
    // SIGSEGV outside any subcase, in the prologue of the second run.
    //
    // The editor never hits it — one TerrainService lives for the process.
    class ScopedTerrain
    {
    public:
        ScopedTerrain(services::TerrainService& owner, services::EntityHandle terrain)
            : service(owner), entity(terrain)
        {
        }

        ~ScopedTerrain() { service.deleteTerrain(entity); }

        ScopedTerrain(const ScopedTerrain&) = delete;
        ScopedTerrain& operator=(const ScopedTerrain&) = delete;

    private:
        services::TerrainService& service;
        services::EntityHandle entity;
    };

    // A 3x1 strip of Low-resolution tiles: wide enough that a corridor running along X crosses
    // more than one tile and therefore exercises seams, small enough to stay fast.
    services::TerrainCreationData stripTerrain()
    {
        services::TerrainCreationData creation;
        creation.tilesX = 3;
        creation.tilesZ = 1;
        creation.resolution = 0; // Low — 33 verts per side
        creation.worldTileSize = 32.0f;
        creation.maxHeight = 100.0f;
        creation.minHeight = -10.0f;
        return creation;
    }

    // A straight polyline down the middle of the strip in Z, at a height nothing else uses.
    std::vector<glm::vec3> corridorAlongX(float height)
    {
        return {glm::vec3(2.0f, height, 16.0f), glm::vec3(90.0f, height, 16.0f)};
    }

    // A perpendicular one, so two layers genuinely disagree where they overlap.
    std::vector<glm::vec3> corridorAlongZ(float height)
    {
        return {glm::vec3(48.0f, height, -12.0f), glm::vec3(48.0f, height, 44.0f)};
    }

    terrain::SplineParams corridorParams(float width, const std::string& roadName)
    {
        terrain::SplineParams params;
        params.corridorWidth = width;
        params.falloffWidth = 4.0f;
        params.embankmentHeight = 0.0f;
        params.roadName = roadName;
        params.ops = terrain::SplineOps::Sculpt;
        return params;
    }

    bool applyLayer(uint64_t splineId, const std::vector<glm::vec3>& samples,
                    const terrain::SplineParams& params)
    {
        events::splineTerrain::ApplySplineDeformCommand cmd;
        cmd.splineSamples = samples;
        cmd.params = params;
        cmd.splineId = splineId;
        return events::EventDispatcher::instance().query(cmd);
    }

    std::vector<services::HeightLayerInfo> stackOf()
    {
        return events::EventDispatcher::instance().query(
            events::splineTerrain::GetHeightLayerStackQuery{});
    }

    // Every tile's height plane, concatenated in a fixed coord order. One value to compare, and it
    // covers the seams the per-tile comparisons would miss.
    std::vector<float> wholeTerrain(services::TerrainService& service)
    {
        auto tiles = service.getAllLoadedTiles();
        std::sort(tiles.begin(), tiles.end(),
                  [](const terrain::TerrainTile* a, const terrain::TerrainTile* b)
                  {
                      if (a->coord.z != b->coord.z)
                          return a->coord.z < b->coord.z;
                      return a->coord.x < b->coord.x;
                  });

        std::vector<float> all;
        for (const terrain::TerrainTile* tile : tiles)
            all.insert(all.end(), tile->heightData.begin(), tile->heightData.end());
        return all;
    }

    [[nodiscard]] bool differsSomewhere(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin());
    }
}

TEST_SUITE("TerrainHeightLayerStackOps")
{
    TEST_CASE("height_layer_stack: every operation round-trips through undo and redo")
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.clear();

        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::TerrainService service(sceneGraph);
        service.registerEventHandlers();

        services::SplineTerrainServiceImpl splineService;
        splineService.registerEventHandlers();

        UndoLog undoLog;
        UndoCaptureGuard undoGuard(undoLog);

        // NOTE: this is the first test in the suite to build a MULTI-tile terrain, and that is
        // what exposed the missing uninitialized-JobSystem guard in JobSystem::parallelFor.
        // createGrid's LOD pass passes minBatchSize 1, so a one-tile terrain took the inline fast
        // path while three tiles went to a scheduler the preceding tests had shut down. Keep the
        // tile count above one: it is the only coverage the engine has for that path.
        const services::EntityHandle terrainEntity = service.createTerrain(stripTerrain());
        REQUIRE(terrainEntity.isValid());

        // Declared here so it is destroyed BEFORE `service`, while the grid it must clean up
        // still exists.
        ScopedTerrain terrainGuard(service, terrainEntity);

        REQUIRE(service.getAllLoadedTiles().size() == 3);

        // A fresh terrain has no sidecar to fail to load, so authoring must be open.
        REQUIRE_FALSE(dispatcher.query(events::terrain::IsHeightLayerEditingLockedQuery{}));

        const std::vector<float> pristine = wholeTerrain(service);

        SUBCASE("apply then delete then undo restores the layer at its original index")
        {
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));
            REQUIRE(applyLayer(2, corridorAlongZ(9.0f), corridorParams(5.0f, "Ridge Cut")));
            REQUIRE(applyLayer(3, corridorAlongX(2.0f), corridorParams(4.0f, "")));

            auto stack = stackOf();
            REQUIRE(stack.size() == 3);
            CHECK(stack[0].id == 1);
            CHECK(stack[1].id == 2);
            CHECK(stack[2].id == 3);
            CHECK(stack[0].name == "Main Road");
            CHECK(stack[1].name == "Ridge Cut");
            // A sculpt-only spline carries no road name, so the layer falls back to its id.
            CHECK(stack[2].name == "Layer 3");

            const std::vector<float> allThree = wholeTerrain(service);
            REQUIRE(differsSomewhere(allThree, pristine));

            // Delete the MIDDLE layer: restoring it at the top would silently put it above a layer
            // it used to compose under, and the plane comparison is what catches that.
            undoLog.pushed.clear();
            events::splineTerrain::DeleteSplineCommand deleteCmd;
            deleteCmd.splineId = 2;
            dispatcher.execute(deleteCmd);

            stack = stackOf();
            REQUIRE(stack.size() == 2);
            CHECK(stack[0].id == 1);
            CHECK(stack[1].id == 3);

            const std::vector<float> withoutTwo = wholeTerrain(service);
            REQUIRE(differsSomewhere(withoutTwo, allThree));

            REQUIRE(undoLog.size() == 1);
            undoLog.last().undo();

            stack = stackOf();
            REQUIRE(stack.size() == 3);
            CHECK(stack[1].id == 2); // back in the middle, not appended to the end
            CHECK(stack[1].name == "Ridge Cut");
            CHECK(wholeTerrain(service) == allThree);

            undoLog.last().execute(); // redo
            CHECK(stackOf().size() == 2);
            CHECK(wholeTerrain(service) == withoutTwo);

            // A second cycle must be identical — the record is a definition, not a delta.
            undoLog.last().undo();
            CHECK(wholeTerrain(service) == allThree);
        }

        SUBCASE("hide and show are bit-exact in both directions")
        {
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));
            const std::vector<float> shown = wholeTerrain(service);
            REQUIRE(differsSomewhere(shown, pristine));

            undoLog.pushed.clear();
            events::splineTerrain::SetHeightLayerVisibleWithUndoCommand hide;
            hide.splineId = 1;
            hide.visible = false;
            REQUIRE(dispatcher.query(hide));

            CHECK_FALSE(stackOf()[0].visible);
            const std::vector<float> hidden = wholeTerrain(service);

            // Hiding the only layer must recompose straight back to the untouched ground: the base
            // was seeded from it and coverage is what keeps the base authoritative.
            CHECK(hidden == pristine);

            REQUIRE(undoLog.size() == 1);
            undoLog.last().undo();
            CHECK(stackOf()[0].visible);
            CHECK(wholeTerrain(service) == shown);

            undoLog.last().execute();
            CHECK(wholeTerrain(service) == hidden);
        }

        SUBCASE("a no-op toggle succeeds but records nothing")
        {
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));

            undoLog.pushed.clear();
            events::splineTerrain::SetHeightLayerVisibleWithUndoCommand show;
            show.splineId = 1;
            show.visible = true; // already visible
            CHECK(dispatcher.query(show));
            CHECK(undoLog.empty()); // an entry that changes nothing still eats a Ctrl+Z
        }

        SUBCASE("reorder changes the overlap and undo puts the permutation back exactly")
        {
            // Two crossing corridors at different heights: where they overlap, whichever composes
            // LAST wins, so the order is observable.
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(8.0f, "A")));
            REQUIRE(applyLayer(2, corridorAlongZ(9.0f), corridorParams(8.0f, "B")));
            REQUIRE(applyLayer(3, corridorAlongX(1.0f), corridorParams(3.0f, "C")));

            const std::vector<float> original = wholeTerrain(service);

            undoLog.pushed.clear();
            events::splineTerrain::MoveHeightLayerWithUndoCommand move;
            move.splineId = 1;
            move.newIndex = 2; // from the bottom of the stack to the top
            REQUIRE(dispatcher.query(move));

            auto stack = stackOf();
            REQUIRE(stack.size() == 3);
            CHECK(stack[0].id == 2);
            CHECK(stack[1].id == 3);
            CHECK(stack[2].id == 1);

            const std::vector<float> reordered = wholeTerrain(service);
            REQUIRE(differsSomewhere(reordered, original));

            REQUIRE(undoLog.size() == 1);
            undoLog.last().undo();

            // std::rotate is a bijection, so moving the layer back restores the WHOLE permutation,
            // including the two rows it slid past.
            stack = stackOf();
            CHECK(stack[0].id == 1);
            CHECK(stack[1].id == 2);
            CHECK(stack[2].id == 3);
            CHECK(wholeTerrain(service) == original);

            undoLog.last().execute();
            CHECK(wholeTerrain(service) == reordered);
        }

        SUBCASE("rename is metadata only — it recomposes nothing")
        {
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));
            const std::vector<float> before = wholeTerrain(service);

            undoLog.pushed.clear();
            events::splineTerrain::RenameHeightLayerWithUndoCommand rename;
            rename.splineId = 1;
            rename.name = "Coast Highway";
            REQUIRE(dispatcher.query(rename));

            CHECK(stackOf()[0].name == "Coast Highway");
            CHECK(wholeTerrain(service) == before);

            // Nothing outstanding: a rename must not queue a single tile of work.
            const auto progress =
                dispatcher.query(events::terrain::GetHeightLayerRecomposeProgressQuery{});
            CHECK(progress.pendingResident == 0);

            REQUIRE(undoLog.size() == 1);
            undoLog.last().undo();
            CHECK(stackOf()[0].name == "Main Road");

            undoLog.last().execute();
            CHECK(stackOf()[0].name == "Coast Highway");

            // Renaming to the same string is not history.
            undoLog.pushed.clear();
            CHECK(dispatcher.query(rename));
            CHECK(undoLog.empty());
        }

        SUBCASE("re-authoring a layer is undone back to the PREVIOUS corridor, not to bare ground")
        {
            // The VK-1648 fix. Undo used to be a visibility flip, which reverted past the artist's
            // earlier corridor instead of back to it.
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(4.0f, "Road")));
            const std::vector<float> narrow = wholeTerrain(service);
            REQUIRE(differsSomewhere(narrow, pristine));

            undoLog.pushed.clear();

            events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
            snapshotQuery.splineId = 1;
            const auto before = dispatcher.query(snapshotQuery);
            REQUIRE(before.present);

            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(11.0f, "Road")));
            const std::vector<float> wide = wholeTerrain(service);
            REQUIRE(differsSomewhere(wide, narrow));
            REQUIRE(stackOf().size() == 1); // updated in place, not stacked

            const auto after = dispatcher.query(snapshotQuery);
            REQUIRE(after.present);

            services::HeightLayerRecordUndoCommand command("Apply Road", before, after);
            command.undo();
            CHECK(stackOf().size() == 1);
            CHECK(wholeTerrain(service) == narrow); // the earlier corridor, not bare ground

            command.execute();
            CHECK(wholeTerrain(service) == wide);
        }

        SUBCASE("overlapping layers are independent of the order they are removed in")
        {
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(8.0f, "A")));
            REQUIRE(applyLayer(2, corridorAlongZ(9.0f), corridorParams(8.0f, "B")));

            events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
            snapshotQuery.splineId = 1;
            const auto snapshotOne = dispatcher.query(snapshotQuery);
            REQUIRE(snapshotOne.present);

            const std::vector<float> both = wholeTerrain(service);

            // Remove the layer that composes FIRST — out of application order, which is exactly
            // the case a snapshot-based undo used to get wrong.
            events::splineTerrain::RemoveSplineHeightLayerCommand remove;
            remove.splineId = 1;
            dispatcher.execute(remove);

            const std::vector<float> onlyTwo = wholeTerrain(service);
            REQUIRE(differsSomewhere(onlyTwo, both));

            events::splineTerrain::RestoreHeightLayerCommand restore;
            restore.snapshot = snapshotOne;
            REQUIRE(dispatcher.query(restore));

            auto stack = stackOf();
            REQUIRE(stack.size() == 2);
            CHECK(stack[0].id == 1); // back UNDER layer 2, where it was
            CHECK(stack[1].id == 2);
            CHECK(wholeTerrain(service) == both);
        }

        SUBCASE("the progress query reports idle immediately after a synchronous stack op")
        {
            // Layer operations compose synchronously and unbudgeted, so pendingResident is already
            // zero by the time anything can poll. Pinning that here keeps the panel's "what does
            // the bar actually show" comment honest.
            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));

            const auto progress =
                dispatcher.query(events::terrain::GetHeightLayerRecomposeProgressQuery{});
            CHECK(progress.pendingResident == 0);
            CHECK(progress.pendingUnloaded == 0);
        }

        SUBCASE("the stack query reports what the Editor needs and nothing it cannot hold")
        {
            // AC: the Editor drives all of this through Services. The DTO must therefore survive a
            // plain copy with no Terrain type in sight.
            static_assert(std::is_copy_constructible_v<services::HeightLayerInfo>);

            REQUIRE(applyLayer(1, corridorAlongX(5.0f), corridorParams(6.0f, "Main Road")));

            const std::vector<services::HeightLayerInfo> copy = stackOf();
            REQUIRE(copy.size() == 1);
            CHECK(copy[0].id == 1);
            CHECK(copy[0].order == 0);
            CHECK(copy[0].visible);
            CHECK(copy[0].name == "Main Road");
            CHECK(copy[0].affectedTileCount > 0);
        }

        // No trailing dispatcher.clear(): the teardown order that matters is ScopedTerrain ->
        // UndoCaptureGuard -> splineService -> service, and each of those releases its own
        // handlers. Clearing here would instead have run deleteTerrain with the handler table
        // already empty. The next case that needs a clean table clears on the way IN, which is
        // the convention test_terrain_layer_weight_query.cpp follows.
    }
}
