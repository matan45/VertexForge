#include <doctest.h>

#include <threading/JobSystem.hpp>
#include <threading/CancellationToken.hpp>
#include <threading/ParallelReduce.hpp>
#include <threading/TaskGraphBuilder.hpp>
#include <threading/TaskGraph.hpp>

#include <atomic>
#include <array>
#include <cstdint>
#include <thread>
#include <vector>

// CPU-only unit tests for the threading/job system (Tier 1: pooled submit,
// JobHandle continuations, cached-TaskSet TaskGraph, parallelReduce).

namespace {
	// Initialize the process-wide JobSystem exactly once for the whole test run.
	void ensureJobSystem()
	{
		static const bool initialized = []() {
			threading::JobSystem::instance().init();
			return true;
		}();
		(void)initialized;
	}
}

TEST_SUITE("JobSystem") {

	TEST_CASE("parallelFor: covers the full range exactly once") {
		ensureJobSystem();

		const uint32_t count = 100000;
		std::vector<uint32_t> touched(count, 0);

		threading::JobSystem::instance().parallelFor(count,
			[&touched](uint32_t begin, uint32_t end) {
				for (uint32_t i = begin; i < end; ++i) {
					touched[i] += 1;
				}
			});

		uint64_t total = 0;
		bool allOnce = true;
		for (uint32_t v : touched) {
			total += v;
			if (v != 1) allOnce = false;
		}
		CHECK(allOnce);
		CHECK(total == count);
	}

	TEST_CASE("submit: returns a future carrying the result") {
		ensureJobSystem();

		auto future = threading::JobSystem::instance().submit([]() { return 7 * 6; });
		CHECK(future.get() == 42);
	}

	TEST_CASE("submit: propagates exceptions through the future") {
		ensureJobSystem();

		auto future = threading::JobSystem::instance().submit([]() -> int {
			throw std::runtime_error("boom");
		});
		CHECK_THROWS_AS(future.get(), std::runtime_error);
	}

	TEST_CASE("submit: pooled tasks survive heavy concurrent submission") {
		ensureJobSystem();

		// Several external threads each fire many fire-and-forget jobs. Exercises the
		// lock-light slot pool acquire/recycle path and external-thread registration.
		constexpr int kThreads = 4;
		constexpr int kPerThread = 5000;
		std::atomic<int64_t> counter{0};

		std::vector<std::thread> producers;
		producers.reserve(kThreads);
		for (int t = 0; t < kThreads; ++t) {
			producers.emplace_back([&counter]() {
				std::vector<std::future<void>> futures;
				futures.reserve(kPerThread);
				for (int i = 0; i < kPerThread; ++i) {
					futures.push_back(threading::JobSystem::instance().submit(
						[&counter]() { counter.fetch_add(1, std::memory_order_relaxed); }));
				}
				for (auto& f : futures) f.get();
			});
		}
		for (auto& p : producers) p.join();

		CHECK(counter.load() == static_cast<int64_t>(kThreads) * kPerThread);
	}

	TEST_CASE("submitJob + wait: job runs and handle reports completion") {
		ensureJobSystem();

		std::atomic<bool> ran{false};
		auto handle = threading::JobSystem::instance().submitJob([&ran]() {
			ran.store(true);
		});
		threading::JobSystem::instance().wait(handle);

		CHECK(ran.load());
		CHECK(handle.isComplete());
	}

	TEST_CASE("then: continuation runs strictly after its dependency") {
		ensureJobSystem();
		auto& js = threading::JobSystem::instance();

		std::atomic<int> ticket{0};
		int orderA = -1;
		int orderB = -1;

		auto a = js.submitJob([&]() { orderA = ticket.fetch_add(1); });
		auto b = js.then(a, [&]() { orderB = ticket.fetch_add(1); });
		js.wait(b);

		CHECK(orderA == 0);
		CHECK(orderB == 1);
	}

	TEST_CASE("then: on an invalid handle still schedules the work") {
		ensureJobSystem();

		std::atomic<bool> ran{false};
		threading::JobHandle invalid;
		auto h = threading::JobSystem::instance().then(invalid, [&]() { ran.store(true); });
		threading::JobSystem::instance().wait(h);
		CHECK(ran.load());
	}

	TEST_CASE("whenAll: fires only after every dependency completes") {
		ensureJobSystem();
		auto& js = threading::JobSystem::instance();

		std::atomic<int> finished{0};
		auto a = js.submitJob([&]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			finished.fetch_add(1);
		});
		auto b = js.submitJob([&]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			finished.fetch_add(1);
		});

		int observedAtJoin = -1;
		std::array<threading::JobHandle, 2> deps{ a, b };
		auto c = js.whenAll(deps, [&]() { observedAtJoin = finished.load(); });
		js.wait(c);

		CHECK(finished.load() == 2);
		CHECK(observedAtJoin == 2);
	}

	TEST_CASE("whenAll: with no dependencies runs immediately") {
		ensureJobSystem();

		std::atomic<bool> ran{false};
		std::array<threading::JobHandle, 0> none{};
		auto h = threading::JobSystem::instance().whenAll(none, [&]() { ran.store(true); });
		threading::JobSystem::instance().wait(h);
		CHECK(ran.load());
	}

	TEST_CASE("submitJob: a token cancelled before start skips the work but still completes") {
		ensureJobSystem();
		auto& js = threading::JobSystem::instance();

		auto token = threading::CancellationToken::create();
		token->cancel(); // cancelled before the job is even scheduled

		std::atomic<bool> ran{false};
		auto handle = js.submitJob([&ran]() { ran.store(true); }, token);
		js.wait(handle); // must still unblock

		CHECK_FALSE(ran.load());
		CHECK(handle.isComplete());
	}

	TEST_CASE("submitJob: a live token runs the work normally") {
		ensureJobSystem();
		auto& js = threading::JobSystem::instance();

		auto token = threading::CancellationToken::create();
		std::atomic<bool> ran{false};
		auto handle = js.submitJob([&ran]() { ran.store(true); }, token);
		js.wait(handle);

		CHECK(ran.load());
	}

	TEST_CASE("parallelReduce: matches the sequential fold") {
		ensureJobSystem();

		const uint32_t count = 200000;
		int64_t serial = 0;
		for (uint32_t i = 0; i < count; ++i) serial += static_cast<int64_t>(i);

		int64_t parallel = threading::parallelReduce<int64_t>(count, 0,
			[](uint32_t begin, uint32_t end, int64_t& acc) {
				for (uint32_t i = begin; i < end; ++i) acc += static_cast<int64_t>(i);
			},
			[](int64_t lhs, int64_t rhs) { return lhs + rhs; });

		CHECK(parallel == serial);
	}

	TEST_CASE("parallelReduce: empty range returns the identity") {
		ensureJobSystem();

		int64_t result = threading::parallelReduce<int64_t>(0, 123,
			[](uint32_t, uint32_t, int64_t&) {},
			[](int64_t lhs, int64_t rhs) { return lhs + rhs; });
		CHECK(result == 123);
	}
}

TEST_SUITE("TaskGraph") {

	TEST_CASE("execute: honors a linear dependency chain") {
		ensureJobSystem();

		std::atomic<int> ticket{0};
		int orderA = -1, orderB = -1, orderC = -1;

		threading::TaskGraphBuilder builder;
		auto a = builder.task("A", [&]() { orderA = ticket.fetch_add(1); });
		auto b = builder.task("B", [&]() { orderB = ticket.fetch_add(1); });
		auto c = builder.task("C", [&]() { orderC = ticket.fetch_add(1); });
		builder.depends(b, a); // B after A
		builder.depends(c, b); // C after B

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(false);

		CHECK(orderA == 0);
		CHECK(orderB == 1);
		CHECK(orderC == 2);
	}

	TEST_CASE("execute: cached task sets are reusable across frames") {
		ensureJobSystem();

		std::atomic<int> runs{0};
		threading::TaskGraphBuilder builder;
		// Two independent worker tasks (a multi-task layer exercises the dispatch path).
		builder.task("W0", [&]() { runs.fetch_add(1); });
		builder.task("W1", [&]() { runs.fetch_add(1); });

		auto graph = builder.build();
		REQUIRE(graph != nullptr);

		for (int frame = 0; frame < 10; ++frame) {
			graph->execute(false);
		}
		CHECK(runs.load() == 20);
	}

	TEST_CASE("build: rejects a cyclic graph") {
		ensureJobSystem();

		threading::TaskGraphBuilder builder;
		auto x = builder.task("X", []() {});
		auto y = builder.task("Y", []() {});
		builder.depends(x, y);
		builder.depends(y, x); // cycle

		auto graph = builder.build();
		CHECK(graph == nullptr);
	}

	TEST_CASE("execute: profiling populates per-task timing") {
		ensureJobSystem();

		threading::TaskGraphBuilder builder;
		builder.task("P0", []() {});
		builder.task("P1", []() {});
		auto graph = builder.build();
		REQUIRE(graph != nullptr);

		graph->execute(true);
		const auto& profile = graph->getProfileData();
		REQUIRE(profile.size() == 2);
		for (const auto& entry : profile) {
			CHECK(entry.endTimeNs >= entry.startTimeNs);
		}
	}
}
