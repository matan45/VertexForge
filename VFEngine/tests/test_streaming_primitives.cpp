#include <doctest.h>
#include <streaming/FrameBudget.hpp>
#include <streaming/StreamingPriority.hpp>
#include <streaming/AsyncLoadQueue.hpp>
#include <streaming/AsyncResultSlot.hpp>
#include <streaming/BudgetedEvictionPool.hpp>

#include <chrono>
#include <cstdlib>
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

    // VK-1600: int keys stand in for sectorRegistrationId / HLODCellCoord - the pool is
    // agnostic, and an int makes the eviction ORDER assertions readable.
    using Pool = streaming::BudgetedEvictionPool<int>;
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

    // ---- BudgetedEvictionPool (VK-1600) ----

    TEST_CASE("BudgetedEvictionPool with a zero capacity is inert")
    {
        // 0 = unlimited is the sentinel every streaming budget uses, and it must reproduce
        // pre-VK-1600 behaviour exactly: admit everything, evict nothing.
        Pool pool;
        std::vector<int> evicted;

        CHECK(pool.unlimited());
        for (int i = 0; i < 50; ++i)
        {
            CHECK(pool.admit(i, 1000, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        }

        CHECK(evicted.empty());
        CHECK(pool.size() == 50);
        CHECK(pool.residentCost() == 50000);
        CHECK_FALSE(pool.overBudget());
        CHECK(pool.evictionCount() == 0);
    }

    TEST_CASE("BudgetedEvictionPool picks victims least-recently-used first")
    {
        // Three equally valuable residents; a clearly better candidate needs room for one.
        // Which one goes is the LRU question.
        Pool pool;
        pool.setCapacity(300);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, /*frame*/ 30, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(2, 100, /*frame*/ 10, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(3, 100, /*frame*/ 20, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(evicted.empty());

        CHECK(pool.admit(4, 100, 40, 5.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(evicted.size() == 1);
        CHECK(evicted[0] == 2); // frame 10 - the oldest, not the first inserted
        CHECK(pool.residentCost() == 300);
        CHECK(pool.evictionCount() == 1);
    }

    TEST_CASE("BudgetedEvictionPool never evicts a pinned entry")
    {
        Pool pool;
        pool.setCapacity(200);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(2, 100, 2, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.setPinned(1, true));
        REQUIRE(pool.setPinned(2, true));

        // A far better candidate still cannot touch pinned residents.
        CHECK(pool.admit(3, 100, 3, 99.0f, evicted) == Pool::Admission::Refused);
        CHECK(evicted.empty());

        // ...and neither can a shrink: trim leaves the pool legitimately over budget.
        pool.setCapacity(50);
        pool.trim(evicted);
        CHECK(evicted.empty());
        CHECK(pool.overBudget());
        CHECK(pool.residentCost() == 200);

        // Unpin one and the shrink can finally make progress.
        REQUIRE(pool.setPinned(2, false));
        pool.trim(evicted);
        REQUIRE(evicted.size() == 1);
        CHECK(evicted[0] == 2);
    }

    TEST_CASE("BudgetedEvictionPool shrink evicts to fit, worst and oldest first")
    {
        Pool pool;
        pool.setCapacity(400);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 5, 3.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(2, 100, 5, 1.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(3, 100, 1, 2.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(4, 100, 9, 2.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.residentCost() == 400);

        pool.setCapacity(150); // setCapacity alone decides nothing - the caller executes
        CHECK(pool.residentCost() == 400);
        CHECK(pool.size() == 4);

        pool.trim(evicted);
        REQUIRE(evicted.size() == 3);
        CHECK(evicted[0] == 2); // priority 1 - least valuable
        CHECK(evicted[1] == 3); // priority 2, frame 1 - older of the tie
        CHECK(evicted[2] == 4); // priority 2, frame 9
        CHECK(pool.contains(1)); // priority 3 - the most valuable survivor
        CHECK(pool.residentCost() == 100);
        CHECK_FALSE(pool.overBudget());
    }

    TEST_CASE("BudgetedEvictionPool refuses rather than evicting something better")
    {
        // The livelock guard: a candidate worse than everything resident must not displace
        // anything, or the caller would drop bytes it immediately re-requests.
        Pool pool;
        pool.setCapacity(200);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 10.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(2, 100, 1, 10.0f, evicted) == Pool::Admission::Admitted);

        CHECK(pool.admit(3, 100, 2, 5.0f, evicted) == Pool::Admission::Refused);  // worse
        CHECK(pool.admit(4, 100, 2, 10.0f, evicted) == Pool::Admission::Refused); // equal
        CHECK(evicted.empty());
        CHECK(pool.evictionCount() == 0);
        CHECK(pool.size() == 2);
    }

    TEST_CASE("BudgetedEvictionPool refusal is atomic")
    {
        // A candidate that can only be part-funded must evict NOTHING - a refusal that still
        // cost residency is the worst of both worlds.
        Pool pool;
        pool.setCapacity(300);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 1.0f, evicted) == Pool::Admission::Admitted); // beatable
        REQUIRE(pool.admit(2, 100, 1, 9.0f, evicted) == Pool::Admission::Admitted); // not
        REQUIRE(pool.admit(3, 100, 1, 9.0f, evicted) == Pool::Admission::Admitted); // not

        // Needs 200 freed but only entry 1 (100) is a legal victim.
        CHECK(pool.admit(4, 200, 2, 5.0f, evicted) == Pool::Admission::Refused);
        CHECK(evicted.empty());
        CHECK(pool.contains(1));
        CHECK(pool.residentCost() == 300);
    }

    TEST_CASE("BudgetedEvictionPool refuses an entry larger than the whole pool")
    {
        Pool pool;
        pool.setCapacity(100);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 50, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        // Emptying the pool still would not make room, so it must not try.
        CHECK(pool.admit(2, 500, 2, 99.0f, evicted) == Pool::Admission::Refused);
        CHECK(evicted.empty());
        CHECK(pool.contains(1));
    }

    TEST_CASE("BudgetedEvictionPool hysteresis absorbs a marginal improvement")
    {
        Pool pool;
        pool.setCapacity(100);
        pool.setHysteresisMargin(1.0f);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 0.0f, evicted) == Pool::Admission::Admitted);

        // Better, but not by more than the margin: the resident keeps its slot.
        CHECK(pool.admit(2, 100, 2, 0.5f, evicted) == Pool::Admission::Refused);
        CHECK(pool.admit(3, 100, 2, 1.0f, evicted) == Pool::Admission::Refused);
        CHECK(evicted.empty());

        // Clear of the margin: the swap goes through.
        CHECK(pool.admit(4, 100, 3, 1.5f, evicted) == Pool::Admission::Admitted);
        REQUIRE(evicted.size() == 1);
        CHECK(evicted[0] == 1);
    }

    TEST_CASE("BudgetedEvictionPool re-admitting a resident key re-ranks without evicting")
    {
        Pool pool;
        pool.setCapacity(200);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        CHECK(pool.admit(1, 150, 7, 4.0f, evicted) == Pool::Admission::AlreadyResident);
        CHECK(evicted.empty());

        const auto* entry = pool.find(1);
        REQUIRE(entry != nullptr);
        CHECK(entry->cost == 150);
        CHECK(entry->lastUsedFrame == 7);
        CHECK(entry->priority == doctest::Approx(4.0f));
        CHECK(pool.residentCost() == 150);
    }

    TEST_CASE("BudgetedEvictionPool resize keeps the running cost exact")
    {
        // The prefetch consumer admits at the .vfsector header size and corrects to the real
        // buffer size once the read lands.
        Pool pool;
        pool.setCapacity(1000);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 400, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        CHECK(pool.resize(1, 250));
        CHECK(pool.residentCost() == 250);
        CHECK_FALSE(pool.resize(99, 10)); // absent key

        CHECK(pool.remove(1));
        CHECK(pool.residentCost() == 0);
        CHECK_FALSE(pool.remove(1));
    }

    TEST_CASE("BudgetedEvictionPool works as a count budget")
    {
        // Consumer (c): cost 1 per entry turns the byte pool into a "max loaded sectors" cap.
        Pool pool;
        pool.setCapacity(3);
        std::vector<int> evicted;

        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(pool.admit(i, 1, 1, static_cast<float>(-i), evicted) ==
                    Pool::Admission::Admitted);
        }
        CHECK(pool.residentCost() == 3);

        // Nearer than the worst resident (-2), so it displaces exactly one.
        CHECK(pool.admit(9, 1, 2, -0.5f, evicted) == Pool::Admission::Admitted);
        REQUIRE(evicted.size() == 1);
        CHECK(evicted[0] == 2);
        CHECK(pool.size() == 3);
    }

    TEST_CASE("BudgetedEvictionPool degrades gracefully when the budget is under the ring")
    {
        // THE livelock case. A ring of 8 sectors against a 3-sector budget: once the nearest
        // three are resident, every further candidate is worse, so the pool must go quiet
        // instead of trading blobs back and forth (each trade is a real disk read).
        Pool pool;
        pool.setCapacity(3);
        pool.setHysteresisMargin(1.0f);
        std::vector<int> evicted;

        constexpr int ringSize = 8;
        // Sector i sits i sectors from the camera; priority is negated distance.
        auto priorityAt = [](int sector, int cameraSector)
        { return -static_cast<float>(std::abs(sector - cameraSector)); };

        for (uint64_t frame = 1; frame <= 60; ++frame)
        {
            for (int sector = 0; sector < ringSize; ++sector)
            {
                const float p = priorityAt(sector, 0);
                if (pool.contains(sector))
                    pool.touch(sector, frame, p);
                else
                    pool.admit(sector, 1, frame, p, evicted);
            }
        }

        CHECK(pool.size() == 3);
        CHECK(pool.contains(0));
        CHECK(pool.contains(1));
        CHECK(pool.contains(2));
        // Filling an empty pool is not eviction; after that, nothing may churn.
        CHECK(pool.evictionCount() == 0);
        CHECK(evicted.empty());

        // Now move the camera to the far end. The set must migrate ONCE and then go quiet.
        auto runFrames = [&](uint64_t first, uint64_t last)
        {
            for (uint64_t frame = first; frame <= last; ++frame)
            {
                for (int sector = 0; sector < ringSize; ++sector)
                {
                    const float p = priorityAt(sector, ringSize - 1);
                    if (pool.contains(sector))
                        pool.touch(sector, frame, p);
                    else
                        pool.admit(sector, 1, frame, p, evicted);
                }
            }
        };

        runFrames(61, 70);
        const uint64_t afterMigration = pool.evictionCount();

        runFrames(71, 200);

        CHECK(pool.size() == 3);
        CHECK(pool.contains(ringSize - 1));
        CHECK(pool.contains(ringSize - 2));
        CHECK(pool.contains(ringSize - 3));
        // THE assertion: 130 further frames of the same ring cost ZERO further evictions.
        // The migration itself is bounded by one pass over the ring, not by frame count -
        // an oscillating pool would keep climbing here forever.
        CHECK(pool.evictionCount() == afterMigration);
        CHECK(afterMigration <= static_cast<uint64_t>(ringSize));
    }

    TEST_CASE("BudgetedEvictionPool clear resets residency and churn")
    {
        Pool pool;
        pool.setCapacity(100);
        std::vector<int> evicted;

        REQUIRE(pool.admit(1, 100, 1, 0.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.admit(2, 100, 2, 5.0f, evicted) == Pool::Admission::Admitted);
        REQUIRE(pool.evictionCount() == 1);

        pool.clear();
        CHECK(pool.empty());
        CHECK(pool.residentCost() == 0);
        CHECK(pool.evictionCount() == 0);
        CHECK(pool.capacity() == 100); // capacity is configuration, not residency
    }
}
