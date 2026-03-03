#pragma once
#include <future>
#include <functional>
#include <type_traits>
#include <cstdint>

namespace threading {

	enum class JobPriority : uint32_t {
		HIGH = 0,    // Frame-critical work
		NORMAL = 1,  // Regular async (resource loading, imports)
		LOW = 2      // Background streaming, saves
	};

	class JobSystem {
	public:
		static JobSystem& instance();

		void init(uint32_t threadCount = 0); // 0 = auto (hw_concurrency - 2, min 2)
		void shutdown();

		// Submit a callable, returns std::future<T> (drop-in replacement for std::async)
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

		// Internal: enqueues a type-erased task on the enkiTS scheduler
		void submitTask(std::function<void()> func, JobPriority priority);

		struct Impl;
		std::unique_ptr<Impl> pImpl;
	};

}
