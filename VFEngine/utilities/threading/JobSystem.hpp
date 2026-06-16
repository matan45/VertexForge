#pragma once
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
	class JobHandle {
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

	class JobSystem {
	public:
		static JobSystem& instance();

		void init(uint32_t threadCount = 0, uint32_t maxExternalThreads = 8);
		void shutdown();

		uint32_t getThreadCount() const;

		// Access the underlying enkiTS scheduler (used by TaskGraph for dependency-based execution)
		enki::TaskScheduler* getScheduler();

		void parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end)>& body,
			uint32_t minBatchSize = 64);

		// Overload that exposes the enkiTS thread index for per-thread local storage
		void parallelFor(uint32_t count, const std::function<void(uint32_t begin, uint32_t end, uint32_t threadNum)>& body,
			uint32_t minBatchSize = 64);

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

}
