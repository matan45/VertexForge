#pragma once
#include "CancellationToken.hpp"
#include "ThreadingExport.hpp"

#include <future>
#include <functional>
#include <memory>
#include <span>
#include <type_traits>
#include <cstdint>

namespace enki { class TaskScheduler; }

namespace threading {

	enum class JobPriority : uint32_t {
		HIGH = 0,
		NORMAL = 1,
		LOW = 2
	};

	// Internal control block backing a JobHandle. Defined in JobSystem.cpp.
	struct JobControl;

	// Lightweight, copyable token referring to a submitted job (see submitJob/then/whenAll).
	// A default-constructed handle is invalid and reports complete.
#pragma warning(push)
#pragma warning(disable: 4251) // STL members crossing the DLL boundary (shared_ptr/unique_ptr/function)
	class VF_THREADING_API JobHandle {
	public:
		JobHandle() = default;

		bool valid() const { return static_cast<bool>(control); }

		// True if the job has finished (or the handle is invalid/empty).
		bool isComplete() const;

		// Block the calling thread until the job completes. No-op if invalid.
		void wait() const;

	private:
		friend class JobSystem;
		std::shared_ptr<JobControl> control;
	};

	class VF_THREADING_API JobSystem {
	public:
		static JobSystem& instance();

		void init(uint32_t threadCount = 0, uint32_t maxExternalThreads = 8);
		void shutdown();

		// Total threads registered with the scheduler (task threads + external slots + 1).
		uint32_t getThreadCount() const;

		// Compute-worker concurrency: created task threads + 1 (the calling/main thread).
		// Excludes the external-thread slots, so suitable as a max-parallelism estimate
		// for fork/join work (e.g. the Jolt physics adapter's GetMaxConcurrency).
		uint32_t getWorkerThreadCount() const;

		// True when the caller is the thread enkiTS registered as thread 0 - i.e. whoever called
		// init() (MainLoop / RuntimeBootstrap, both on the process main thread). Pinned TaskGraph
		// tasks (TaskGraphBuilder's enki::LambdaPinnedTask(0u, ...)) run exclusively on that
		// thread, so this is the check a main-thread-only system uses to assert it was scheduled
		// correctly. Returns true when the JobSystem was never initialized: with no worker pool
		// every job path falls back to inline execution on the caller, so the caller IS the only
		// thread (the Tests.exe case).
		[[nodiscard]] bool isMainThread() const;

		// Access the underlying enkiTS scheduler (used by TaskGraph for dependency-based execution)
		enki::TaskScheduler* getScheduler();

		// Lean fire-and-forget dispatch (no future, no handle). Routes through the same
		// pooled task path as submit(); runs inline if the system is not initialized.
		void enqueue(std::function<void()> fn, JobPriority priority = JobPriority::NORMAL);

		// Synchronous fork/join: splits [0,count) across workers and blocks until done. The
		// calling thread participates while it waits, so parallelFor is safe to call from
		// inside another task/parallelFor (nested fork/join) - the calling worker keeps
		// running tasks during WaitforTask, no deadlock. Priority defaults to HIGH because the
		// caller is blocked on the result (the latency-critical path); fire-and-forget/async
		// APIs default to NORMAL instead.
		void parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end)>& body,
			uint32_t minBatchSize = 64, JobPriority priority = JobPriority::HIGH);

		// Overload that exposes the enkiTS thread index for per-thread local storage
		void parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end, uint32_t threadNum)>& body,
			uint32_t minBatchSize = 64, JobPriority priority = JobPriority::HIGH);

		// Asynchronous fork/join: dispatches the parallelFor onto a worker and returns a handle
		// immediately (no inline wait). Use with then()/whenAll()/wait() to fork-join without
		// blocking the caller - e.g. to overlap a parallel loop with other TaskGraph work.
		JobHandle parallelForAsync(uint32_t count, std::function<void(uint32_t begin, uint32_t end)> body,
			uint32_t minBatchSize = 64, JobPriority priority = JobPriority::HIGH);

		// Fire-and-forget submit returning a std::future for the callable's result.
		// Hot path: dispatches through a lock-light pooled task (no per-call heap TaskSet).
		template<typename F>
		auto submit(F&& callable, JobPriority priority = JobPriority::NORMAL)
			-> std::future<std::invoke_result_t<std::decay_t<F>>>
		{
			using ReturnType = std::invoke_result_t<std::decay_t<F>>;

			auto promise = std::make_shared<std::promise<ReturnType>>();
			auto future = promise->get_future();

			submitTask(
				[callable = std::forward<F>(callable), promise]() mutable {
					try {
						if constexpr (std::is_void_v<ReturnType>) {
							callable();
							promise->set_value();
						}
						else {
							promise->set_value(callable());
						}
					}
					catch (...) {
						promise->set_exception(std::current_exception());
					}
				},
				priority
			);

			return future;
		}

		// --- Continuation / dependency API (no polling) ---

		// Submit a void job and return a handle usable with then()/whenAll()/wait().
		JobHandle submitJob(std::function<void()> fn, JobPriority priority = JobPriority::NORMAL);

		// As submitJob, but skips fn if the token is already cancelled when the job starts
		// (the handle still completes so waiters unblock). fn may also poll the token.
		JobHandle submitJob(std::function<void()> fn, CancellationToken::Ptr token,
			JobPriority priority = JobPriority::NORMAL);

		// Run fn after dep completes. If dep is already complete (or invalid) fn is scheduled immediately.
		JobHandle then(const JobHandle& dep, std::function<void()> fn, JobPriority priority = JobPriority::NORMAL);

		// Run fn after all deps complete. Empty/all-invalid deps schedule fn immediately.
		JobHandle whenAll(std::span<const JobHandle> deps, std::function<void()> fn,
			JobPriority priority = JobPriority::NORMAL);

		// Block until the job completes.
		void wait(const JobHandle& handle);

	private:
		JobSystem();
		~JobSystem();
		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		void submitTask(std::function<void()> func, JobPriority priority);

		struct Impl;
		std::unique_ptr<Impl> pImpl;
	};
#pragma warning(pop)

}
