#pragma once
#include <future>
#include <functional>
#include <type_traits>
#include <cstdint>

namespace enki { class TaskScheduler; }

namespace threading {

	enum class JobPriority : uint32_t {
		HIGH = 0,
		NORMAL = 1,
		LOW = 2
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
