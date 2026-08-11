#include <doctest.h>
#include <streaming/FrameBudget.hpp>
#include <streaming/StreamingPriority.hpp>
#include <streaming/AsyncLoadQueue.hpp>
#include <streaming/AsyncResultSlot.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// utilities/streaming: the shared primitives the streaming
// systems (sector, terrain, lights, GPU objects) build on
// ============================================================

namespace
{
    // VK-1592: stand-in for AsyncSectorLoadResult. Its default must read as
    // "nothing happened" - that is what AsyncResultSlot's destructor delivers.
    struct SlotResult
    {
        int value = 0;
        bool abandoned = true;
    };
}

TEST_SUITE("StreamingPrimitives")
{
    // ---- FrameBudget ----

    TEST_CASE("FrameBudget consumes counts until exhausted")
    {
        streaming::FrameBudget budget(3);

        CHECK(budget.tryConsume());
        CHECK(budget.tryConsume());
        CHECK(budget.countRemaining() == 1);
        CHECK(budget.tryConsume());
        CHECK_FALSE(budget.tryConsume());
        CHECK(budget.exhausted());

        budget.reset();
        CHECK(budget.countRemaining() == 3);
        CHECK(budget.tryConsume());
    }

    TEST_CASE("FrameBudget enforces the byte allowance jointly with the count")
    {
        streaming::FrameBudget budget(8, 1000);

        CHECK(budget.tryConsume(1, 600));
        CHECK_FALSE(budget.tryConsume(1, 600)); // would exceed bytes
        CHECK(budget.bytesRemaining() == 400);  // failed consume took nothing
        CHECK(budget.countRemaining() == 7);
        CHECK(budget.tryConsume(1, 400));
        CHECK(budget.exhausted()); // bytes gone even though counts remain
    }

    TEST_CASE("FrameBudget with zero count refuses all work")
    {
        streaming::FrameBudget budget(0);
        CHECK_FALSE(budget.tryConsume());
        CHECK(budget.exhausted());
    }

    // ---- WeightedPriority ----

    TEST_CASE("WeightedPriority reproduces the light-manager formula")
    {
        // distanceFactor*1.0 + intensity*0.5 + radius*0.3 + shadow*2.0 + staticBonus
        float distance = 100.0f;
        float priority = streaming::WeightedPriority()
                             .addInverseDistance(distance, 1.0f, 0.01f)
                             .add(2.0f, 0.5f)  // intensity
                             .add(10.0f, 0.3f) // radius
                             .addIf(true, 2.0f)  // casts shadow (weight as bonus)
                             .addIf(true, 0.3f)  // static bonus
                             .value();

        float expected = 1.0f / (1.0f + 100.0f * 0.01f) + 2.0f * 0.5f + 10.0f * 0.3f + 2.0f + 0.3f;
        CHECK(priority == doctest::Approx(expected));
    }

    TEST_CASE("WeightedPriority: closer entries score higher")
    {
        auto score = [](float dist)
        {
            return streaming::WeightedPriority().addInverseDistance(dist, 1.0f).value();
        };
        CHECK(score(10.0f) > score(100.0f));
        CHECK(score(100.0f) > score(1000.0f));
    }

    // ---- HysteresisBand ----

    TEST_CASE("HysteresisBand keeps state inside the band")
    {
        streaming::HysteresisBand band(200.0f, 300.0f);

        CHECK(band.shouldActivate(150.0f));
        CHECK_FALSE(band.shouldActivate(250.0f));
        CHECK_FALSE(band.shouldDeactivate(250.0f)); // in band: no action either way
        CHECK(band.inBand(250.0f));
        CHECK(band.shouldDeactivate(350.0f));
        CHECK_FALSE(band.inBand(350.0f));
    }

    TEST_CASE("HysteresisBand clamps a degenerate exit threshold")
    {
        streaming::HysteresisBand band(300.0f, 200.0f); // exit < enter is invalid
        CHECK(band.exitThreshold >= band.enterThreshold);
        CHECK_FALSE(band.shouldDeactivate(250.0f));
    }

    // ---- PriorityHysteresis ----

    TEST_CASE("PriorityHysteresis keeps entries within the margin of the cutoff")
    {
        streaming::PriorityHysteresis hysteresis{0.05f};

        CHECK(hysteresis.shouldKeepActive(1.00f, 1.00f)); // at cutoff
        CHECK(hysteresis.shouldKeepActive(0.96f, 1.00f)); // within margin
        CHECK_FALSE(hysteresis.shouldKeepActive(0.90f, 1.00f)); // clearly below
    }

    // ---- AsyncLoadQueue ----

    TEST_CASE("AsyncLoadQueue delivers completed results once")
    {
        streaming::AsyncLoadQueue<int, std::string> queue;

        std::promise<std::string> promise;
        REQUIRE(queue.launch(7, promise.get_future()));
        CHECK(queue.contains(7));
        CHECK_FALSE(queue.launch(7, std::future<std::string>{})); // already pending

        std::vector<std::pair<int, std::string>> delivered;
        auto collect = [&](int key, std::string result)
        { delivered.emplace_back(key, std::move(result)); };

        queue.poll(collect);
        CHECK(delivered.empty()); // not ready yet

        promise.set_value("payload");
        queue.poll(collect);
        REQUIRE(delivered.size() == 1);
        CHECK(delivered[0].first == 7);
        CHECK(delivered[0].second == "payload");
        CHECK(queue.empty());

        queue.poll(collect);
        CHECK(delivered.size() == 1); // no double delivery
    }

    TEST_CASE("AsyncLoadQueue discards cancelled results")
    {
        streaming::AsyncLoadQueue<int, int> queue;

        std::promise<int> promise;
        queue.launch(1, promise.get_future());
        queue.cancel(1);
        promise.set_value(42);

        int deliveries = 0;
        queue.poll([&](int, int) { ++deliveries; });
        CHECK(deliveries == 0);
        CHECK(queue.empty()); // the slot is freed even though the result was dropped
    }

    TEST_CASE("AsyncLoadQueue::drain blocks out all pending work")
    {
        streaming::AsyncLoadQueue<int, int> queue;

        queue.launch(1, std::async(std::launch::async, [] { return 1; }));
        queue.launch(2, std::async(std::launch::async, [] { return 2; }));
        REQUIRE(queue.size() == 2);

        queue.drain();
        CHECK(queue.empty());
    }

    TEST_CASE("AsyncLoadQueue keys stay independent")
    {
        streaming::AsyncLoadQueue<int, int> queue;

        std::promise<int> first;
        std::promise<int> second;
        queue.launch(1, first.get_future());
        queue.launch(2, second.get_future());

        second.set_value(20);
        std::vector<int> keys;
        queue.poll([&](int key, int) { keys.push_back(key); });
        REQUIRE(keys.size() == 1);
        CHECK(keys[0] == 2);
        CHECK(queue.contains(1)); // still in flight

        first.set_value(10);
        queue.poll([&](int key, int) { keys.push_back(key); });
        CHECK(keys.size() == 2);
    }

    // ---- AsyncResultSlot (VK-1592) ----

    TEST_CASE("AsyncResultSlot delivers the first fulfil and ignores the rest")
    {
        streaming::AsyncResultSlot<SlotResult> slot;
        auto future = slot.getFuture();

        slot.fulfil({7, false});
        slot.fulfil({99, false}); // a losing racer must not throw future_error

        REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        auto result = future.get();
        CHECK(result.value == 7);
        CHECK_FALSE(result.abandoned);
    }

    TEST_CASE("AsyncResultSlot destructor resolves a slot that never ran")
    {
        // This is the whole point: the scheduler can drop a LoadRequest without ever calling
        // executeLoad, and a broken promise would make AsyncLoadQueue::poll() throw and
        // drain() block forever.
        std::future<SlotResult> future;
        {
            auto slot = std::make_shared<streaming::AsyncResultSlot<SlotResult>>();
            future = slot->getFuture();
            CHECK(future.wait_for(std::chrono::seconds(0)) != std::future_status::ready);
        }

        REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        CHECK(future.get().abandoned); // default-constructed
    }

    TEST_CASE("AsyncResultSlot fulfilled before destruction keeps its value")
    {
        std::future<SlotResult> future;
        {
            streaming::AsyncResultSlot<SlotResult> slot;
            future = slot.getFuture();
            slot.fulfil({3, false});
        } // ~AsyncResultSlot must not overwrite with the default

        CHECK(future.get().value == 3);
    }

    TEST_CASE("AsyncResultSlot drives an AsyncLoadQueue end to end")
    {
        streaming::AsyncLoadQueue<int, SlotResult> queue;
        auto slot = std::make_shared<streaming::AsyncResultSlot<SlotResult>>();
        REQUIRE(queue.launch(5, slot->getFuture()));

        int deliveries = 0;
        SlotResult seen;
        auto collect = [&](int, SlotResult r) { ++deliveries; seen = r; };

        queue.poll(collect);
        CHECK(deliveries == 0); // producer has not fulfilled yet

        slot->fulfil({11, false});
        queue.poll(collect);
        REQUIRE(deliveries == 1);
        CHECK(seen.value == 11);
        CHECK(queue.empty());
    }

    TEST_CASE("AsyncResultSlot lets drain finish a request that never dispatched")
    {
        // Models the teardown path: the slot's only strong reference dies with the dropped
        // request, so drain() finds a ready future instead of blocking.
        streaming::AsyncLoadQueue<int, SlotResult> queue;
        {
            auto slot = std::make_shared<streaming::AsyncResultSlot<SlotResult>>();
            REQUIRE(queue.launch(9, slot->getFuture()));
        }

        queue.drain();
        CHECK(queue.empty());
    }
}
