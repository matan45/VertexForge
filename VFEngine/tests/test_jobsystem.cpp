#include <doctest.h>

#include <threading/JobSystem.hpp>
#include <threading/CancellationToken.hpp>
#include <threading/ParallelReduce.hpp>
#include <threading/ParallelView.hpp>
#include <threading/TaskGraphBuilder.hpp>
#include <threading/TaskGraph.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

// CPU-only unit tests for the threading/job system (Tier 1: pooled submit,
// JobHandle continuations, cached-TaskSet TaskGraph, parallelReduce).

namespace {
	// RAII scope that brings the process-wide JobSystem up for the duration of a single
	// test case and tears it down again. This keeps the singleton initialized ONLY while
	// these tests run: other test files (which expect submit() to execute inline because
	// the JobSystem is uninitialized) must not inherit a live worker pool from us, or
	// their non-thread-safe code paths would suddenly run concurrently. init()/shutdown()
	// are re-entrant on the underlying enkiTS scheduler, so per-case cycling is safe.
	struct JobScope {
		JobScope() { threading::JobSystem::instance().init(); }
		~JobScope() { threading::JobSystem::instance().shutdown(); }
	};
}

TEST_SUITE("JobSystem") {

	TEST_CASE("parallelFor: covers the full range exactly once") {
		JobScope jobScope;

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
		JobScope jobScope;

		auto future = threading::JobSystem::instance().submit([]() { return 7 * 6; });
		CHECK(future.get() == 42);
	}

	TEST_CASE("submit: propagates exceptions through the future") {
		JobScope jobScope;

		auto future = threading::JobSystem::instance().submit([]() -> int {
			throw std::runtime_error("boom");
		});
		CHECK_THROWS_AS(future.get(), std::runtime_error);
	}

	TEST_CASE("submit: pooled tasks survive heavy concurrent submission") {
		JobScope jobScope;

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
		JobScope jobScope;

		std::atomic<bool> ran{false};
		auto handle = threading::JobSystem::instance().submitJob([&ran]() {
			ran.store(true);
		});
		threading::JobSystem::instance().wait(handle);

		CHECK(ran.load());
		CHECK(handle.isComplete());
	}

	TEST_CASE("then: continuation runs strictly after its dependency") {
		JobScope jobScope;
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
		JobScope jobScope;

		std::atomic<bool> ran{false};
		threading::JobHandle invalid;
		auto h = threading::JobSystem::instance().then(invalid, [&]() { ran.store(true); });
		threading::JobSystem::instance().wait(h);
		CHECK(ran.load());
	}

	TEST_CASE("whenAll: fires only after every dependency completes") {
		JobScope jobScope;
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
		JobScope jobScope;

		std::atomic<bool> ran{false};
		std::array<threading::JobHandle, 0> none{};
		auto h = threading::JobSystem::instance().whenAll(none, [&]() { ran.store(true); });
		threading::JobSystem::instance().wait(h);
		CHECK(ran.load());
	}

	TEST_CASE("submitJob: a token cancelled before start skips the work but still completes") {
		JobScope jobScope;
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
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		auto token = threading::CancellationToken::create();
		std::atomic<bool> ran{false};
		auto handle = js.submitJob([&ran]() { ran.store(true); }, token);
		js.wait(handle);

		CHECK(ran.load());
	}

	TEST_CASE("submitJob: a throwing body is contained and the handle still completes") {
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		// enkiTS has no exception handling, so an unguarded throw out of the job body
		// would std::terminate the whole test process. The body must be caught/logged
		// and control->complete() must still run so wait() unblocks and isComplete()==true.
		auto handle = js.submitJob([]() { throw std::runtime_error("boom"); });
		js.wait(handle); // must not terminate; must return
		CHECK(handle.isComplete());
	}

	TEST_CASE("then: a throwing continuation still completes its handle") {
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		auto a = js.submitJob([]() {});
		auto b = js.then(a, []() { throw std::runtime_error("boom in continuation"); });
		js.wait(b); // throwing continuation must not hang the waiter or terminate
		CHECK(b.isComplete());
	}

	TEST_CASE("parallelReduce: matches the sequential fold") {
		JobScope jobScope;

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
		JobScope jobScope;

		int64_t result = threading::parallelReduce<int64_t>(0, 123,
			[](uint32_t, uint32_t, int64_t&) {},
			[](int64_t lhs, int64_t rhs) { return lhs + rhs; });
		CHECK(result == 123);
	}
}

TEST_SUITE("TaskGraph") {

	TEST_CASE("execute: honors a linear dependency chain") {
		JobScope jobScope;

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
		JobScope jobScope;

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
		JobScope jobScope;

		threading::TaskGraphBuilder builder;
		auto x = builder.task("X", []() {});
		auto y = builder.task("Y", []() {});
		builder.depends(x, y);
		builder.depends(y, x); // cycle

		auto graph = builder.build();
		CHECK(graph == nullptr);
	}

	TEST_CASE("execute: profiling populates per-task timing") {
		JobScope jobScope;

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

	TEST_CASE("execute: diamond respects both join edges") {
		JobScope jobScope;

		// A -> {B, C} -> D. Native dependencies must keep A first, D last, and B/C after A.
		std::atomic<int> ticket{0};
		std::atomic<int> orderA{-1}, orderB{-1}, orderC{-1}, orderD{-1};

		threading::TaskGraphBuilder builder;
		auto a = builder.task("A", [&]() { orderA = ticket.fetch_add(1); });
		auto b = builder.task("B", [&]() { orderB = ticket.fetch_add(1); });
		auto c = builder.task("C", [&]() { orderC = ticket.fetch_add(1); });
		auto d = builder.task("D", [&]() { orderD = ticket.fetch_add(1); });
		builder.depends(b, a);
		builder.depends(c, a);
		builder.depends(d, b);
		builder.depends(d, c);

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(false);

		CHECK(orderA.load() == 0);
		CHECK(orderB.load() > orderA.load());
		CHECK(orderC.load() > orderA.load());
		CHECK(orderD.load() == 3);
	}

	TEST_CASE("execute: independent branches overlap (no per-layer barrier)") {
		JobScope jobScope;

		// KEY regression for VK-1385 3.A. Chain A1->A2->A3 sets a flag that an INDEPENDENT
		// task B (no shared edge) is waiting on. The old layer-barrier grouped A1 and B in
		// layer 0 and blocked there until BOTH finished, so B waiting on A3 (a later layer)
		// would deadlock. Native dependencies let the A-chain run while B waits -> B unblocks.
		// Needs >=2 task-running threads (one parks in B's wait while another runs the chain).
		if (threading::JobSystem::instance().getWorkerThreadCount() < 2) {
			WARN("skipped: needs >= 2 worker threads to observe cross-branch overlap");
			return;
		}

		std::mutex m;
		std::condition_variable cv;
		bool flag = false;
		std::atomic<bool> bSawFlag{false};

		threading::TaskGraphBuilder builder;
		auto a1 = builder.task("A1", []() {});
		auto a2 = builder.task("A2", []() {});
		auto a3 = builder.task("A3", [&]() {
			{
				std::lock_guard<std::mutex> lk(m);
				flag = true;
			}
			cv.notify_all();
		});
		builder.task("B", [&]() {
			std::unique_lock<std::mutex> lk(m);
			bSawFlag = cv.wait_for(lk, std::chrono::seconds(5), [&]() { return flag; });
		});
		builder.depends(a2, a1);
		builder.depends(a3, a2);

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(false); // must return; would hang under the old barrier

		CHECK(bSawFlag.load());
	}

	TEST_CASE("execute: isolated node (root and leaf) still runs and is waited on") {
		JobScope jobScope;

		std::atomic<int> ticket{0};
		std::atomic<int> orderX{-1}, orderY{-1};
		std::atomic<bool> ranZ{false};

		threading::TaskGraphBuilder builder;
		auto x = builder.task("X", [&]() { orderX = ticket.fetch_add(1); });
		auto y = builder.task("Y", [&]() { orderY = ticket.fetch_add(1); });
		builder.task("Z", [&]() { ranZ = true; }); // isolated: no edges
		builder.depends(y, x);

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(false);

		CHECK(ranZ.load());
		CHECK(orderX.load() >= 0);
		CHECK(orderY.load() > orderX.load());
	}

	TEST_CASE("execute: single node runs once per frame") {
		JobScope jobScope;

		std::atomic<int> runs{0};
		threading::TaskGraphBuilder builder;
		builder.task("Solo", [&]() { runs.fetch_add(1); });

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(false);
		graph->execute(false);

		CHECK(runs.load() == 2);
	}

	TEST_CASE("execute: pinned task runs on the main thread before its dependent") {
		JobScope jobScope;

		std::atomic<int> ticket{0};
		std::atomic<int> orderP{-1}, orderD{-1};

		threading::TaskGraphBuilder builder;
		auto p = builder.pinnedTask("P", [&]() { orderP = ticket.fetch_add(1); });
		auto d = builder.task("D", [&]() { orderD = ticket.fetch_add(1); });
		builder.depends(d, p); // D after P

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		graph->execute(true);

		CHECK(orderP.load() == 0);
		CHECK(orderD.load() == 1);

		const auto& profile = graph->getProfileData();
		REQUIRE(profile.size() == 2);
		CHECK(profile[p].threadId == 0); // pinned tasks always record thread 0 (main)
	}

	TEST_CASE("execute: multi-leaf terminal waits for every leaf each frame") {
		JobScope jobScope;

		// R -> L1, R -> L2. The terminal sentinel depends on both leaves; each frame must run
		// all three exactly once, so over 5 frames each leaf runs 5 times.
		std::atomic<int> runsL1{0}, runsL2{0};
		threading::TaskGraphBuilder builder;
		auto r = builder.task("R", []() {});
		auto l1 = builder.task("L1", [&]() { runsL1.fetch_add(1); });
		auto l2 = builder.task("L2", [&]() { runsL2.fetch_add(1); });
		builder.depends(l1, r);
		builder.depends(l2, r);

		auto graph = builder.build();
		REQUIRE(graph != nullptr);
		for (int frame = 0; frame < 5; ++frame) {
			graph->execute(false);
		}

		CHECK(runsL1.load() == 5);
		CHECK(runsL2.load() == 5);
	}
}

TEST_SUITE("ParallelFor") {

	TEST_CASE("parallelFor: explicit priority still covers the full range") {
		JobScope jobScope;

		const uint32_t count = 100000;
		std::vector<uint32_t> touched(count, 0);

		threading::JobSystem::instance().parallelFor(count,
			[&touched](uint32_t begin, uint32_t end) {
				for (uint32_t i = begin; i < end; ++i) touched[i] += 1;
			},
			64, threading::JobPriority::LOW);

		uint64_t total = std::accumulate(touched.begin(), touched.end(), uint64_t{0});
		CHECK(total == count);
		CHECK(std::all_of(touched.begin(), touched.end(), [](uint32_t v) { return v == 1; }));
	}

	TEST_CASE("parallelForAsync: handle completes and covers the range exactly once") {
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		const uint32_t count = 100000;
		std::vector<uint32_t> touched(count, 0);

		auto handle = js.parallelForAsync(count,
			[&touched](uint32_t begin, uint32_t end) {
				for (uint32_t i = begin; i < end; ++i) touched[i] += 1;
			});
		CHECK(handle.valid());
		js.wait(handle);

		CHECK(handle.isComplete());
		uint64_t total = std::accumulate(touched.begin(), touched.end(), uint64_t{0});
		CHECK(total == count);
		CHECK(std::all_of(touched.begin(), touched.end(), [](uint32_t v) { return v == 1; }));
	}

	TEST_CASE("parallelForAsync: zero count completes without invoking the body") {
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		std::atomic<bool> ran{false};
		auto handle = js.parallelForAsync(0, [&ran](uint32_t, uint32_t) { ran.store(true); });
		js.wait(handle);

		CHECK(handle.isComplete());
		CHECK_FALSE(ran.load());
	}

	TEST_CASE("parallelFor: nested inside a job does not deadlock") {
		JobScope jobScope;
		auto& js = threading::JobSystem::instance();

		const uint32_t count = 50000;
		std::atomic<uint64_t> sum{0};

		// A worker job that itself forks a parallelFor: the calling worker participates in the
		// inner WaitforTask, so this must complete rather than deadlock.
		auto handle = js.submitJob([&]() {
			js.parallelFor(count, [&sum](uint32_t begin, uint32_t end) {
				uint64_t local = 0;
				for (uint32_t i = begin; i < end; ++i) local += i;
				sum.fetch_add(local, std::memory_order_relaxed);
			});
		});
		js.wait(handle);

		uint64_t expected = static_cast<uint64_t>(count - 1) * count / 2;
		CHECK(sum.load() == expected);
	}
}

// Validates the concurrency contract VK-1386 WS1 (animation eval gather) relies on:
// a serial loop that produces an ORDERED output list plus order-independent counters can be
// parallelized via parallelFor(threadNum) using a per-index result buffer (deterministic order)
// and per-thread-local accumulators (histogram / culled count), then merged on one thread, with
// output byte-identical to the serial baseline. The production code (RuntimeAnimatorSystemEval.cpp)
// uses exactly this shape; this test locks the shape under contention without needing animation assets.
TEST_SUITE("ParallelGather") {

	namespace {
		enum class Kind : uint8_t { None, Eval, Interp };

		struct Decision {
			bool culled = false;   // culled entities produce no output and no histogram bump
			uint8_t lod = 0;       // LOD bucket [0,4) for non-culled entities
			Kind kind = Kind::None;
		};

		// Pure function of the index, so serial and parallel MUST agree exactly.
		Decision classify(uint32_t i) {
			Decision d;
			if (i % 7u == 0u) { d.culled = true; return d; }
			d.lod = static_cast<uint8_t>((i / 3u) % 4u);
			if (i % 5u == 0u && d.lod == 1u)      d.kind = Kind::Interp;
			else if (i % 2u == 0u)                d.kind = Kind::Eval;
			else                                  d.kind = Kind::None; // counted, no output
			return d;
		}

		struct GatherResult {
			std::vector<std::pair<uint32_t, uint8_t>> evalOut;   // (index, lod) in canonical order
			std::vector<uint32_t> interpOut;                     // index in canonical order
			uint32_t culled = 0;
			std::array<uint32_t, 4> histogram{};
		};

		bool equal(const GatherResult& a, const GatherResult& b) {
			return a.evalOut == b.evalOut && a.interpOut == b.interpOut &&
			       a.culled == b.culled && a.histogram == b.histogram;
		}

		GatherResult runSerial(uint32_t count) {
			GatherResult r;
			for (uint32_t i = 0; i < count; ++i) {
				Decision d = classify(i);
				if (d.culled) { ++r.culled; continue; }
				++r.histogram[d.lod];
				if (d.kind == Kind::Eval)        r.evalOut.push_back({i, d.lod});
				else if (d.kind == Kind::Interp) r.interpOut.push_back(i);
			}
			return r;
		}

		// Mirrors the production merge: per-index slot buffer + thread-local accumulators.
		GatherResult runParallel(uint32_t count) {
			struct Slot { Kind kind = Kind::None; uint8_t lod = 0; };
			std::vector<Slot> perIndex(count);

			const uint32_t slotCount = threading::JobSystem::instance().getThreadCount() + 1;
			std::vector<uint32_t> tlCulled(slotCount, 0);
			std::vector<std::array<uint32_t, 4>> tlHist(slotCount, std::array<uint32_t, 4>{});

			threading::JobSystem::instance().parallelFor(count,
				[&](uint32_t begin, uint32_t end, uint32_t threadNum) {
					uint32_t slot = threadNum < slotCount ? threadNum : 0;
					for (uint32_t i = begin; i < end; ++i) {
						Decision d = classify(i);
						if (d.culled) { ++tlCulled[slot]; continue; }
						++tlHist[slot][d.lod];
						perIndex[i] = { d.kind, d.lod };
					}
				}, 64);

			GatherResult r;
			for (uint32_t s = 0; s < slotCount; ++s) {
				r.culled += tlCulled[s];
				for (uint8_t l = 0; l < 4; ++l) r.histogram[l] += tlHist[s][l];
			}
			for (uint32_t i = 0; i < count; ++i) {
				if (perIndex[i].kind == Kind::Eval)        r.evalOut.push_back({i, perIndex[i].lod});
				else if (perIndex[i].kind == Kind::Interp) r.interpOut.push_back(i);
			}
			return r;
		}
	}

	TEST_CASE("parallel gather merge byte-matches the serial baseline (large, contended)") {
		JobScope jobScope;

		// Run several times: a data race or non-deterministic merge would surface as a mismatch
		// on at least one iteration.
		const uint32_t count = 50000;
		GatherResult serial = runSerial(count);
		for (int iter = 0; iter < 8; ++iter) {
			CHECK(equal(runParallel(count), serial));
		}
	}

	TEST_CASE("parallel gather merge matches serial below the parallel threshold") {
		JobScope jobScope;

		// Sizes around the production fallback boundary (PARALLEL_VIEW_THRESHOLD = 256).
		for (uint32_t count : { 0u, 1u, 7u, 64u, 255u, 256u, 257u }) {
			CHECK(equal(runParallel(count), runSerial(count)));
		}
	}

	TEST_CASE("parallelFor threadNum stays within getThreadCount()+1 (slot-buffer bound)") {
		JobScope jobScope;

		// WS1 sizes its per-thread slot arrays as getThreadCount()+1 and indexes by threadNum.
		// Verify no threadNum ever exceeds that bound, which would be an out-of-bounds write.
		const uint32_t slotCount = threading::JobSystem::instance().getThreadCount() + 1;
		std::atomic<uint32_t> maxThreadNum{0};
		threading::JobSystem::instance().parallelFor(200000,
			[&](uint32_t, uint32_t, uint32_t threadNum) {
				uint32_t prev = maxThreadNum.load(std::memory_order_relaxed);
				while (threadNum > prev &&
				       !maxThreadNum.compare_exchange_weak(prev, threadNum, std::memory_order_relaxed)) {}
			}, 64);

		CHECK(maxThreadNum.load() < slotCount);
	}
}
