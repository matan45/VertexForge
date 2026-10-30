#include <doctest.h>
#include <eqs/EQSQuery.hpp>
#include <eqs/EQSScheduler.hpp>
#include <eqs/EQSTypes.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// VK-1660: eqs::EQSScheduler - whole queries first in first out within a time budget (fake clock),
// Pending / Running / Completed states, release, result expiry and the queue cap.
// ============================================================

namespace
{
    struct FakeClock
    {
        double now = 0.0;

        eqs::EQSScheduler::Clock clock()
        {
            return [this] { return now; };
        }
    };

    // Costs `cost` fake milliseconds per run, records its id in `order`, and can run a hook mid-query.
    class TimedGenerator : public eqs::IEQSGenerator
    {
    public:
        TimedGenerator(FakeClock& clock, double cost, std::vector<int>& order, int id)
            : clock(clock), cost(cost), order(order), id(id)
        {
        }

        bool generate(const eqs::EQSContext&, const eqs::EQSProviderRefs&, std::vector<eqs::EQSCandidate>& out,
                      std::string&) const override
        {
            clock.now += cost;
            order.push_back(id);
            if (whileRunning)
                whileRunning();
            eqs::EQSCandidate candidate;
            candidate.position = glm::vec3(static_cast<float>(id), 0.0f, 0.0f);
            out.push_back(candidate);
            return true;
        }

        std::string getName() const override { return "Timed"; }

        std::function<void()> whileRunning;

    private:
        FakeClock& clock;
        double cost;
        std::vector<int>& order;
        int id;
    };

    std::shared_ptr<const eqs::EQSQueryDef> timedQuery(const std::shared_ptr<TimedGenerator>& generator)
    {
        auto def = std::make_shared<eqs::EQSQueryDef>();
        def->name = "Timed";
        def->generator = generator;
        def->maxResults = 1;
        return def;
    }

    std::shared_ptr<const eqs::EQSQueryDef> timedQuery(FakeClock& clock, double cost, std::vector<int>& order, int id)
    {
        return timedQuery(std::make_shared<TimedGenerator>(clock, cost, order, id));
    }
}

TEST_SUITE("EQSScheduler")
{
    TEST_CASE("a submitted query is Pending until an update runs it, then Completed")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        const auto handle = scheduler.submit(timedQuery(clock, 0.1, order, 1), eqs::EQSContext{});
        REQUIRE(handle.isValid());
        CHECK(scheduler.getResult(handle).status == eqs::EQSQueryStatus::Pending);
        CHECK(scheduler.pendingCount() == 1);

        scheduler.update({}, 2.0f);
        const auto result = scheduler.getResult(handle);
        CHECK(result.status == eqs::EQSQueryStatus::Completed);
        REQUIRE(result.candidates.size() == 1);
        CHECK(result.candidates[0].position.x == doctest::Approx(1.0f));
        CHECK(scheduler.pendingCount() == 0);
        CHECK(scheduler.resultCount() == 1);
    }

    TEST_CASE("update runs whole queries first in first out while the budget lasts, at least one per update")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        const auto first = scheduler.submit(timedQuery(clock, 1.5, order, 1), {});
        const auto second = scheduler.submit(timedQuery(clock, 1.5, order, 2), {});
        const auto third = scheduler.submit(timedQuery(clock, 1.5, order, 3), {});

        scheduler.update({}, 2.0f); // 1.5 ms spent after the first: under budget, so the second runs too
        CHECK(order == std::vector<int>{1, 2});
        CHECK(scheduler.getResult(first).status == eqs::EQSQueryStatus::Completed);
        CHECK(scheduler.getResult(second).status == eqs::EQSQueryStatus::Completed);
        CHECK(scheduler.getResult(third).status == eqs::EQSQueryStatus::Pending);

        scheduler.update({}, 2.0f);
        CHECK(order == std::vector<int>{1, 2, 3});
        CHECK(scheduler.getResult(third).status == eqs::EQSQueryStatus::Completed);

        // A query costing more than the whole budget still runs (alone).
        const auto expensive = scheduler.submit(timedQuery(clock, 10.0, order, 4), {});
        const auto after = scheduler.submit(timedQuery(clock, 0.1, order, 5), {});
        scheduler.update({}, 2.0f);
        CHECK(scheduler.getResult(expensive).status == eqs::EQSQueryStatus::Completed);
        CHECK(scheduler.getResult(after).status == eqs::EQSQueryStatus::Pending);
    }

    TEST_CASE("a query reads as Running while it runs; releasing it then discards its result")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        auto generator = std::make_shared<TimedGenerator>(clock, 0.1, order, 1);
        eqs::EQSQueryHandle handle;
        eqs::EQSQueryStatus seen = eqs::EQSQueryStatus::Pending;
        generator->whileRunning = [&]
        {
            seen = scheduler.getResult(handle).status;
            scheduler.release(handle);
        };
        handle = scheduler.submit(timedQuery(generator), {});

        scheduler.update({}, 2.0f);
        CHECK(seen == eqs::EQSQueryStatus::Running);
        CHECK(scheduler.getResult(handle).status == eqs::EQSQueryStatus::Failed); // released: never stored
        CHECK(scheduler.resultCount() == 0);
    }

    TEST_CASE("release cancels a pending query and drops a finished result")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        const auto cancelled = scheduler.submit(timedQuery(clock, 0.1, order, 1), {});
        const auto kept = scheduler.submit(timedQuery(clock, 0.1, order, 2), {});

        scheduler.release(cancelled);
        CHECK(scheduler.pendingCount() == 1);
        CHECK(scheduler.getResult(cancelled).status == eqs::EQSQueryStatus::Failed);

        scheduler.update({}, 2.0f);
        CHECK(order == std::vector<int>{2});
        CHECK(scheduler.getResult(kept).status == eqs::EQSQueryStatus::Completed);

        scheduler.release(kept);
        CHECK(scheduler.resultCount() == 0);
        CHECK(scheduler.getResult(kept).status == eqs::EQSQueryStatus::Failed);

        scheduler.release(eqs::EQSQueryHandle{}); // ignored
        scheduler.release(eqs::EQSQueryHandle{999});
    }

    TEST_CASE("releaseAll empties the queue and the results")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        const auto done = scheduler.submit(timedQuery(clock, 0.1, order, 1), {});
        scheduler.update({}, 2.0f);
        scheduler.submit(timedQuery(clock, 0.1, order, 2), {});
        REQUIRE(scheduler.resultCount() == 1);
        REQUIRE(scheduler.pendingCount() == 1);

        scheduler.releaseAll();
        CHECK(scheduler.resultCount() == 0);
        CHECK(scheduler.pendingCount() == 0);
        CHECK(scheduler.getResult(done).status == eqs::EQSQueryStatus::Failed);
    }

    TEST_CASE("a finished result expires kResultLifetimeUpdates updates after the one it finished in")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        const auto handle = scheduler.submit(timedQuery(clock, 0.1, order, 1), {});
        scheduler.update({}, 2.0f); // finishes here

        for (uint64_t i = 1; i < eqs::EQSScheduler::kResultLifetimeUpdates; ++i)
            scheduler.update({}, 2.0f);
        CHECK(scheduler.getResult(handle).status == eqs::EQSQueryStatus::Completed);

        scheduler.update({}, 2.0f);
        CHECK(scheduler.getResult(handle).status == eqs::EQSQueryStatus::Failed);
        CHECK(scheduler.resultCount() == 0);
    }

    TEST_CASE("submit refuses a null query and a full queue")
    {
        FakeClock clock;
        std::vector<int> order;
        eqs::EQSScheduler scheduler(clock.clock());
        CHECK_FALSE(scheduler.submit(nullptr, {}).isValid());

        const auto def = timedQuery(clock, 0.0, order, 1);
        for (size_t i = 0; i < eqs::EQSScheduler::kMaxPending; ++i)
            REQUIRE(scheduler.submit(def, {}).isValid());
        CHECK_FALSE(scheduler.submit(def, {}).isValid());
        CHECK(scheduler.pendingCount() == eqs::EQSScheduler::kMaxPending);
    }

    TEST_CASE("unknown handles read as Failed with an error")
    {
        eqs::EQSScheduler scheduler;
        const auto invalid = scheduler.getResult(eqs::EQSQueryHandle{});
        CHECK(invalid.status == eqs::EQSQueryStatus::Failed);
        CHECK_FALSE(invalid.error.empty());
        CHECK(scheduler.getResult(eqs::EQSQueryHandle{12345}).status == eqs::EQSQueryStatus::Failed);
    }
}
