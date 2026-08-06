#include <doctest.h>

#include "impl/editor/UndoRedoServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"

#include <memory>
#include <string>
#include <utility>

// VK-1615: the undo service gained a configurable depth AND a byte ceiling, because a
// terrain stroke snapshots whole tile arrays before and after (one entry ranges from a few
// KB to ~4 MB), so a depth-only cap cannot bound RAM.
//
// These tests drive the real UndoRedoServiceImpl directly rather than through the
// dispatcher wherever possible, so they do not depend on handler-registration order.

namespace
{
    // A command whose reported footprint is whatever the test says it is, so the byte
    // budget can be exercised without allocating megabytes.
    class FakeUndoCommand : public services::IUndoableCommand
    {
    private:
        std::string label;
        size_t footprint;
        int* executeCount;
        int* undoCount;

    public:
        FakeUndoCommand(std::string commandLabel, size_t bytes,
                        int* executes = nullptr, int* undos = nullptr)
            : label(std::move(commandLabel)), footprint(bytes)
              , executeCount(executes), undoCount(undos)
        {
        }

        void execute() override { if (executeCount) ++(*executeCount); }
        void undo() override { if (undoCount) ++(*undoCount); }
        std::string getDescription() const override { return label; }
        size_t getMemoryFootprint() const override { return footprint; }
    };

    std::unique_ptr<services::IUndoableCommand> makeFake(const std::string& label, size_t bytes)
    {
        return std::make_unique<FakeUndoCommand>(label, bytes);
    }

    constexpr size_t oneMiB = 1024ull * 1024ull;
}

TEST_SUITE("UndoHistoryLimits")
{
    TEST_CASE("depth limit trims the oldest undo entries")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(3, 0);

        for (int i = 1; i <= 5; ++i)
            service.pushCommand(makeFake("cmd" + std::to_string(i), 0));

        const auto stats = service.getHistoryStats();
        CHECK(stats.undoCount == 3);
        // The newest survives; the two oldest were dropped.
        CHECK(service.getUndoDescription() == "cmd5");
    }

    TEST_CASE("zero depth means unlimited")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 0);

        for (int i = 0; i < 120; ++i)
            service.pushCommand(makeFake("cmd", 0));

        CHECK(service.getHistoryStats().undoCount == 120);
    }

    TEST_CASE("byte budget trims the oldest undo entries")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 10 * oneMiB);

        for (int i = 1; i <= 5; ++i)
            service.pushCommand(makeFake("cmd" + std::to_string(i), 4 * oneMiB));

        const auto stats = service.getHistoryStats();
        // 5 x 4 MiB = 20 MiB against a 10 MiB ceiling -> only 2 fit (8 MiB).
        CHECK(stats.undoCount == 2);
        CHECK(stats.totalBytes == 8 * oneMiB);
        CHECK(service.getUndoDescription() == "cmd5");
    }

    TEST_CASE("byte budget always keeps at least one entry")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 1);  // 1 byte ceiling

        service.pushCommand(makeFake("huge", 4 * oneMiB));

        // A budget smaller than a single stroke must not leave the user with no undo at
        // all -- the ceiling bounds history, it does not veto the last action.
        CHECK(service.canUndo());
        CHECK(service.getHistoryStats().undoCount == 1);

        // ...and the action just undone must stay redoable, for the same reason.
        REQUIRE(service.undo());
        CHECK(service.canRedo());
        CHECK(service.getHistoryStats().redoCount == 1);
    }

    TEST_CASE("byte accounting returns to zero after clear")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 0);

        service.pushCommand(makeFake("a", 3 * oneMiB));
        service.pushCommand(makeFake("b", 5 * oneMiB));
        CHECK(service.getHistoryStats().totalBytes == 8 * oneMiB);

        service.clear();
        const auto stats = service.getHistoryStats();
        CHECK(stats.undoCount == 0);
        CHECK(stats.redoCount == 0);
        CHECK(stats.totalBytes == 0);
    }

    TEST_CASE("undo moves bytes between stacks without changing the total")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 0);

        service.pushCommand(makeFake("a", 2 * oneMiB));
        service.pushCommand(makeFake("b", 3 * oneMiB));

        REQUIRE(service.undo());
        auto stats = service.getHistoryStats();
        CHECK(stats.undoCount == 1);
        CHECK(stats.redoCount == 1);
        CHECK(stats.totalBytes == 5 * oneMiB);  // totalBytes spans both stacks

        REQUIRE(service.redo());
        stats = service.getHistoryStats();
        CHECK(stats.undoCount == 2);
        CHECK(stats.redoCount == 0);
        CHECK(stats.totalBytes == 5 * oneMiB);
    }

    TEST_CASE("pushing a new command drops the redo stack's bytes")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 0);

        service.pushCommand(makeFake("a", 2 * oneMiB));
        REQUIRE(service.undo());
        REQUIRE(service.getHistoryStats().redoCount == 1);

        service.pushCommand(makeFake("b", 1 * oneMiB));
        const auto stats = service.getHistoryStats();
        CHECK(stats.redoCount == 0);
        CHECK(stats.totalBytes == 1 * oneMiB);  // the discarded redo entry's bytes went too
    }

    TEST_CASE("redo stack is trimmed too")
    {
        // Regression: before VK-1615 the redo stack was never trimmed, so peak residency
        // was 2x the configured depth.
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(4, 0);

        for (int i = 1; i <= 4; ++i)
            service.pushCommand(makeFake("cmd" + std::to_string(i), 4 * oneMiB));

        // Undo everything, then tighten the byte ceiling: the redo history must shed.
        for (int i = 0; i < 4; ++i)
            REQUIRE(service.undo());

        auto stats = service.getHistoryStats();
        REQUIRE(stats.redoCount == 4);
        REQUIRE(stats.totalBytes == 16 * oneMiB);

        service.setHistoryLimits(4, 8 * oneMiB);
        stats = service.getHistoryStats();
        CHECK(stats.redoCount == 2);
        CHECK(stats.totalBytes == 8 * oneMiB);
    }

    TEST_CASE("lowering the depth while entries sit in redo cannot overflow the undo stack")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(5, 0);

        for (int i = 1; i <= 3; ++i)
            service.pushCommand(makeFake("cmd" + std::to_string(i), 0));
        REQUIRE(service.undo());   // undo=2, redo=1

        service.setHistoryLimits(2, 0);
        REQUIRE(service.redo());   // would push undo to 3 without the trim in redo()

        CHECK(service.getHistoryStats().undoCount == 2);
    }

    TEST_CASE("SharedUndoCommand forwards the footprint")
    {
        auto inner = std::make_shared<FakeUndoCommand>("inner", 7 * oneMiB);
        services::SharedUndoCommand wrapper(inner);
        CHECK(wrapper.getMemoryFootprint() == 7 * oneMiB);

        // A null inner must not crash the budget accounting.
        services::SharedUndoCommand empty(nullptr);
        CHECK(empty.getMemoryFootprint() == 0);
    }

    TEST_CASE("BatchUndoCommand sums child footprints")
    {
        services::BatchUndoCommand batch("group");
        CHECK(batch.getMemoryFootprint() == 0);

        batch.addCommand(makeFake("a", 2 * oneMiB));
        batch.addCommand(makeFake("b", 3 * oneMiB));
        CHECK(batch.getMemoryFootprint() == 5 * oneMiB);
    }

    TEST_CASE("a batch is accounted for once, by its children's total")
    {
        services::UndoRedoServiceImpl service;
        service.setHistoryLimits(0, 0);

        service.beginBatch("group");
        service.pushCommand(makeFake("a", 2 * oneMiB));
        service.pushCommand(makeFake("b", 3 * oneMiB));
        // Folded into the batch, not counted yet.
        CHECK(service.getHistoryStats().totalBytes == 0);

        service.endBatch();
        const auto stats = service.getHistoryStats();
        CHECK(stats.undoCount == 1);
        CHECK(stats.totalBytes == 5 * oneMiB);
    }

    TEST_CASE("SetUndoHistoryLimitsCommand and GetUndoHistoryStatsQuery work through the dispatcher")
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Deliberately NOT service.registerEventHandlers(): that claims the process-global
        // Undo/Redo/Push/Batch handler slots, which test_group_transform.cpp owns via a
        // function-local static service (:65-75). Stealing and then releasing them would
        // break whichever of the two TUs doctest runs second. Registering only the two new
        // handlers exercises the same plumbing without touching the shared slots.
        //
        // The service is static so the captured `this` outlives the handler registration
        // even if an unregister is ever missed.
        static services::UndoRedoServiceImpl service;
        service.clear();
        service.setHistoryLimits(0, 0);

        dispatcher.registerCommandHandler<events::undoredo::SetUndoHistoryLimitsCommand>(
            [](const events::undoredo::SetUndoHistoryLimitsCommand& cmd)
            {
                service.setHistoryLimits(cmd.maxDepth, cmd.maxBytes);
            });
        dispatcher.registerQueryHandler<events::undoredo::GetUndoHistoryStatsQuery>(
            [](const events::undoredo::GetUndoHistoryStatsQuery&)
            {
                return service.getHistoryStats();
            });

        {
            events::undoredo::SetUndoHistoryLimitsCommand cmd;
            cmd.maxDepth = 2;
            cmd.maxBytes = 0;
            dispatcher.execute(cmd);
        }

        for (int i = 1; i <= 4; ++i)
            service.pushCommand(makeFake("cmd" + std::to_string(i), 0));

        const auto stats = dispatcher.query(events::undoredo::GetUndoHistoryStatsQuery{});
        CHECK(stats.maxDepth == 2);
        CHECK(stats.maxBytes == 0);
        CHECK(stats.undoCount == 2);

        service.clear();
        dispatcher.unregisterCommandHandler<events::undoredo::SetUndoHistoryLimitsCommand>();
        dispatcher.unregisterQueryHandler<events::undoredo::GetUndoHistoryStatsQuery>();
    }
}
