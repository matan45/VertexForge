// VK-1648 — the four height-layer undo commands, in isolation.
//
// No TerrainGrid, no TerrainService, no Vulkan: every primitive the commands dispatch is faked on
// the real dispatcher and recorded, so each test asserts the EXACT command and payload that undo()
// and execute() emit. Same shape as test_terrain_stroke_undo.cpp, which fakes
// RestoreStrokeStateCommand the same way.
//
// The single most important case here is "a reverse path records nothing". A command that pushed
// while being undone would not merely duplicate history: UndoRedoServiceImpl::pushCommand calls
// clearStackBytes(redoStack), which clears it — so redo would silently stop working.

#include <doctest.h>

#include <data/TerrainLayerUndoCommands.hpp>
#include <data/SplineApplyUndoCommands.hpp>
#include <events/EventDispatcher.hpp>
#include <events/editor/UndoRedoEvents.hpp>
#include <events/terrain/HeightLayerUndoEvents.hpp>
#include <events/terrain/SplineTerrainEvents.hpp>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace
{
    using Restore = events::splineTerrain::RestoreHeightLayerCommand;
    using Remove = events::splineTerrain::RemoveSplineHeightLayerCommand;
    using SetVisible = events::splineTerrain::SetSplineHeightLayerVisibleCommand;
    using Move = events::splineTerrain::MoveHeightLayerCommand;
    using SetName = events::splineTerrain::SetHeightLayerNameCommand;
    using RestoreWeights = events::splineTerrain::RestoreSplineWeightsCommand;
    using Push = events::undoredo::PushUndoableCommand;

    // Everything the fakes saw, in order. Order matters for the record command, which must remove
    // before it restores when an id is being replaced.
    struct PrimitiveLog
    {
        std::vector<Restore> restores;
        std::vector<Remove> removes;
        std::vector<SetVisible> visibilities;
        std::vector<Move> moves;
        std::vector<SetName> names;
        std::vector<RestoreWeights> weights;

        // The recursion guard. Must stay empty for every undo() and execute() below.
        int pushCount = 0;
    };

    // Steals the handler slots for the duration of the test and gives them back afterwards.
    //
    // Deliberately NOT UndoRedoServiceImpl::registerEventHandlers(): that claims the process-global
    // Undo/Redo/Push/Batch slots, which test_group_transform.cpp owns via a function-local static.
    // Only PushUndoableCommand is faked here, and it is released again on the way out.
    //
    // Safe against that static because doctest orders by FILE and test_group_transform sorts before
    // this one, so its lazy registration has already happened and finished. Releasing the slot
    // rather than leaving a no-op behind is the honest end state: a later TU that needed the real
    // handler would get a loud throw instead of silently losing its undo entries.
    class PrimitiveGuard
    {
    public:
        explicit PrimitiveGuard(PrimitiveLog& log)
        {
            auto& d = events::EventDispatcher::instance();

            d.unregisterQueryHandler<Restore>();
            d.registerQueryHandler<Restore>([&log](const Restore& cmd)
                                            { log.restores.push_back(cmd); return true; });

            d.unregisterCommandHandler<Remove>();
            d.registerCommandHandler<Remove>([&log](const Remove& cmd)
                                             { log.removes.push_back(cmd); });

            d.unregisterQueryHandler<SetVisible>();
            d.registerQueryHandler<SetVisible>([&log](const SetVisible& cmd)
                                               { log.visibilities.push_back(cmd); return true; });

            d.unregisterQueryHandler<Move>();
            d.registerQueryHandler<Move>([&log](const Move& cmd)
                                         { log.moves.push_back(cmd); return true; });

            d.unregisterQueryHandler<SetName>();
            d.registerQueryHandler<SetName>([&log](const SetName& cmd)
                                            { log.names.push_back(cmd); return true; });

            d.unregisterCommandHandler<RestoreWeights>();
            d.registerCommandHandler<RestoreWeights>([&log](const RestoreWeights& cmd)
                                                     { log.weights.push_back(cmd); });

            d.unregisterCommandHandler<Push>();
            d.registerCommandHandler<Push>([&log](const Push&) { ++log.pushCount; });
        }

        ~PrimitiveGuard()
        {
            auto& d = events::EventDispatcher::instance();
            d.unregisterQueryHandler<Restore>();
            d.unregisterCommandHandler<Remove>();
            d.unregisterQueryHandler<SetVisible>();
            d.unregisterQueryHandler<Move>();
            d.unregisterQueryHandler<SetName>();
            d.unregisterCommandHandler<RestoreWeights>();
            d.unregisterCommandHandler<Push>();
        }

        PrimitiveGuard(const PrimitiveGuard&) = delete;
        PrimitiveGuard& operator=(const PrimitiveGuard&) = delete;
    };

    events::splineTerrain::HeightLayerSnapshot makeSnapshot(uint64_t id, uint32_t index,
                                                            bool visible, std::string name,
                                                            float corridorWidth)
    {
        events::splineTerrain::HeightLayerSnapshot snapshot;
        snapshot.present = true;
        snapshot.id = id;
        snapshot.index = index;
        snapshot.visible = visible;
        snapshot.name = std::move(name);
        snapshot.type = terrain::HeightLayerType::SplineCorridor;
        snapshot.spline.corridor.corridorWidth = corridorWidth;
        snapshot.spline.corridor.falloffWidth = 2.0f;
        snapshot.spline.samples = {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(32.0f, 1.0f, 0.0f)};
        snapshot.affected.insert(terrain::TileCoord(0, 0));
        snapshot.affected.insert(terrain::TileCoord(1, 0));
        return snapshot;
    }
}

TEST_SUITE("TerrainLayerUndoCommands")
{
    TEST_CASE("terrain_layer_undo: an ADD undoes to a removal and redoes to a restore")
    {
        PrimitiveLog log;
        PrimitiveGuard guard(log);

        const auto after = makeSnapshot(5, 0, true, "Main Road", 6.0f);
        services::HeightLayerRecordUndoCommand command(
            "Apply Spline", events::splineTerrain::HeightLayerSnapshot{}, after);
        REQUIRE(command.hasChange());

        command.undo();
        CHECK(log.restores.empty());
        REQUIRE(log.removes.size() == 1);
        CHECK(log.removes[0].splineId == 5);

        command.execute();
        REQUIRE(log.restores.size() == 1);
        CHECK(log.restores[0].snapshot.present);
        CHECK(log.restores[0].snapshot.id == 5);
        CHECK(log.restores[0].snapshot.name == "Main Road");
        CHECK(log.restores[0].snapshot.spline.corridor.corridorWidth == doctest::Approx(6.0f));

        // A second cycle must be identical: the command replays a definition, not a delta.
        command.undo();
        REQUIRE(log.removes.size() == 2);
        CHECK(log.removes[1].splineId == 5);

        CHECK(log.pushCount == 0);
    }

    TEST_CASE("terrain_layer_undo: a DELETE undoes to a restore at the ORIGINAL index")
    {
        PrimitiveLog log;
        PrimitiveGuard guard(log);

        // Index 2, not 0. Restoring at the top would silently put the layer above things it used
        // to compose under, which is a different terrain.
        const auto before = makeSnapshot(9, 2, false, "Ridge Cut", 3.5f);
        services::HeightLayerRecordUndoCommand command(
            "Delete Height Layer", before, events::splineTerrain::HeightLayerSnapshot{});

        command.undo();
        REQUIRE(log.restores.size() == 1);
        CHECK(log.restores[0].snapshot.index == 2);
        CHECK(log.restores[0].snapshot.id == 9);
        CHECK(log.restores[0].snapshot.affected.size() == 2);

        // Visibility travels with the record: undoing the delete of a HIDDEN layer must not
        // un-hide it.
        CHECK_FALSE(log.restores[0].snapshot.visible);

        command.execute();
        REQUIRE(log.removes.size() == 1);
        CHECK(log.removes[0].splineId == 9);

        CHECK(log.pushCount == 0);
    }

    TEST_CASE("terrain_layer_undo: an in-place UPDATE undoes to the previous parameters")
    {
        PrimitiveLog log;
        PrimitiveGuard guard(log);

        // This is the case the old visibility-flip got wrong: it hid the layer, reverting past the
        // artist's earlier corridor instead of back to it.
        const auto before = makeSnapshot(4, 1, true, "Main Road", 5.0f);
        const auto after = makeSnapshot(4, 1, true, "Main Road", 12.0f);
        services::HeightLayerRecordUndoCommand command("Apply Road", before, after);

        command.undo();
        REQUIRE(log.restores.size() == 1);
        CHECK(log.restores[0].snapshot.spline.corridor.corridorWidth == doctest::Approx(5.0f));
        CHECK(log.removes.empty()); // never removed — the layer existed on both sides

        command.execute();
        REQUIRE(log.restores.size() == 2);
        CHECK(log.restores[1].snapshot.spline.corridor.corridorWidth == doctest::Approx(12.0f));

        CHECK(log.pushCount == 0);
    }

    TEST_CASE("terrain_layer_undo: an operation that touched no layer reports no change")
    {
        // A paint- or mesh-only spline apply reaches the push site with both endpoints absent. An
        // entry that does nothing on undo is worse than no entry at all: it eats a Ctrl+Z.
        services::HeightLayerRecordUndoCommand command(
            "Apply Spline", events::splineTerrain::HeightLayerSnapshot{},
            events::splineTerrain::HeightLayerSnapshot{});
        CHECK_FALSE(command.hasChange());
    }

    TEST_CASE("terrain_layer_undo: visibility, order and name are parametric round trips")
    {
        PrimitiveLog log;
        PrimitiveGuard guard(log);

        SUBCASE("hide / show")
        {
            services::HeightLayerVisibilityUndoCommand command("Hide Height Layer", 8, true, false);

            command.undo();
            REQUIRE(log.visibilities.size() == 1);
            CHECK(log.visibilities[0].splineId == 8);
            CHECK(log.visibilities[0].visible);

            command.execute();
            REQUIRE(log.visibilities.size() == 2);
            CHECK_FALSE(log.visibilities[1].visible);
        }

        SUBCASE("reorder replays the index it came from")
        {
            // moveLayer is a std::rotate of one element, so moving the same layer back to its old
            // index restores the exact permutation — including every row the move slid past.
            services::HeightLayerOrderUndoCommand command("Reorder Height Layer", 8, 3, 0);

            command.undo();
            REQUIRE(log.moves.size() == 1);
            CHECK(log.moves[0].splineId == 8);
            CHECK(log.moves[0].newIndex == 3);

            command.execute();
            REQUIRE(log.moves.size() == 2);
            CHECK(log.moves[1].newIndex == 0);
        }

        SUBCASE("rename")
        {
            services::HeightLayerNameUndoCommand command("Rename Height Layer", 8, "Old", "New");

            command.undo();
            REQUIRE(log.names.size() == 1);
            CHECK(log.names[0].splineId == 8);
            CHECK(log.names[0].name == "Old");

            command.execute();
            REQUIRE(log.names.size() == 2);
            CHECK(log.names[1].name == "New");
        }

        CHECK(log.pushCount == 0);
    }

    TEST_CASE("terrain_layer_undo: footprints are stable and proportional to what is held")
    {
        // UndoRedoServiceImpl::getHistoryStats() recomputes every footprint under _DEBUG and logs
        // on drift, so an unstable number is a false alarm on every single call.
        const auto before = makeSnapshot(2, 0, true, "A", 4.0f);
        auto after = makeSnapshot(2, 0, true, "A much longer layer name", 4.0f);
        after.spline.samples.resize(500, glm::vec3(1.0f));

        services::HeightLayerRecordUndoCommand heavy("Apply", before, after);
        const size_t first = heavy.getMemoryFootprint();
        CHECK(heavy.getMemoryFootprint() == first);
        CHECK(heavy.getMemoryFootprint() == first);

        services::HeightLayerRecordUndoCommand light(
            "Apply", events::splineTerrain::HeightLayerSnapshot{}, before);
        CHECK(light.getMemoryFootprint() < first);

        // The parametric three hold no payload worth counting beyond their strings.
        services::HeightLayerVisibilityUndoCommand flip("Hide Height Layer", 2, true, false);
        CHECK(flip.getMemoryFootprint() < 256);
        CHECK(flip.getMemoryFootprint() == flip.getMemoryFootprint());
    }

    TEST_CASE("spline_apply_undo: the height half restores a definition, never a visibility flip")
    {
        PrimitiveLog log;
        PrimitiveGuard guard(log);

        SUBCASE("a first apply is undone by REMOVING the layer, not by hiding it")
        {
            // Hiding left a row in the stack for a layer the artist never knowingly created — and
            // it dispatched through execute() on a query-registered command, which throws.
            const auto after = makeSnapshot(1, 0, true, "New Road", 5.0f);
            services::SplineApplyUndoCommand command("Apply Spline", 1, {}, after, {}, {});
            REQUIRE(command.hasSnapshots());

            command.undo();
            CHECK(log.visibilities.empty());
            REQUIRE(log.removes.size() == 1);
            CHECK(log.removes[0].splineId == 1);

            command.execute();
            REQUIRE(log.restores.size() == 1);
            CHECK(log.restores[0].snapshot.id == 1);
        }

        SUBCASE("re-authoring a road is undone back to the PREVIOUS corridor")
        {
            const auto before = makeSnapshot(1, 0, true, "Road", 5.0f);
            const auto after = makeSnapshot(1, 0, true, "Road", 20.0f);
            services::SplineApplyUndoCommand command("Apply Road", 1, before, after, {}, {});

            command.undo();
            REQUIRE(log.restores.size() == 1);
            CHECK(log.restores[0].snapshot.spline.corridor.corridorWidth == doctest::Approx(5.0f));
            CHECK(log.removes.empty());
        }

        SUBCASE("a paint-only apply touches no layer at all")
        {
            events::splineTerrain::SplineWeightSnapshot weightsBefore;
            weightsBefore[terrain::TileCoord(0, 0)] = terrain::TileWeightMapData{};

            services::SplineApplyUndoCommand command("Apply Spline", 3, {}, {},
                                                     weightsBefore, weightsBefore);
            REQUIRE(command.hasSnapshots());

            command.undo();
            CHECK(log.removes.empty());
            CHECK(log.restores.empty());
            CHECK(log.visibilities.empty());
            CHECK(log.weights.size() == 1);
        }

        CHECK(log.pushCount == 0);
    }
}
