#include "../print/Log.hpp"
#include "JobSystem.hpp"

#include <TaskScheduler.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <vector>

namespace threading {

	struct JobSystem::Impl {
		enki::TaskScheduler scheduler;
		std::mutex pendingMutex;
		std::vector<std::unique_ptr<enki::TaskSet>> pendingTasks;

		void collectCompleted()
		{
			auto it = std::remove_if(pendingTasks.begin(), pendingTasks.end(),
				[](const std::unique_ptr<enki::TaskSet>& task) {
					return task->GetIsComplete();
				}
			);
			pendingTasks.erase(it, pendingTasks.end());
		}
	};

	JobSystem::JobSystem() : pImpl(std::make_unique<Impl>()) {}
	JobSystem::~JobSystem() = default;

	JobSystem& JobSystem::instance()
	{
		static JobSystem inst;
		return inst;
	}

	void JobSystem::init(uint32_t threadCount, uint32_t maxExternalThreads)
	{
		if (threadCount == 0) {
			uint32_t hw = std::thread::hardware_concurrency();
			threadCount = (hw > 3) ? (hw - 2) : 2;
		}

		enki::TaskSchedulerConfig config;
		config.numTaskThreadsToCreate = threadCount;
		config.numExternalTaskThreads = maxExternalThreads;
		pImpl->scheduler.Initialize(config);
		vfLogInfo("JobSystem initialized with {} threads, {} external slots",
			pImpl->scheduler.GetNumTaskThreads(), maxExternalThreads);
	}

	uint32_t JobSystem::getThreadCount() const
	{
		return pImpl->scheduler.GetNumTaskThreads();
	}

	void JobSystem::shutdown()
	{
		pImpl->scheduler.WaitforAllAndShutdown();

		std::lock_guard<std::mutex> lock(pImpl->pendingMutex);
		pImpl->pendingTasks.clear();

		vfLogInfo("JobSystem shutdown complete");
	}

	void JobSystem::submitTask(std::function<void()> func, JobPriority priority)
	{
		// Auto-register external threads (e.g. detached std::thread, std::async)
		if (pImpl->scheduler.GetThreadNum() == enki::NO_THREAD_NUM) {
			if (!pImpl->scheduler.RegisterExternalTaskThread()) {
				vfLogError("[JobSystem] Failed to register external thread - all {} external slots exhausted. "
					"Increase maxExternalThreads in JobSystem::init()", pImpl->scheduler.GetNumRegisteredExternalTaskThreads());
				// Run the task inline as a fallback to avoid undefined behaviour
				func();
				return;
			}
			// Ensure deregistration when thread exits
			thread_local struct Guard {
				enki::TaskScheduler* s = nullptr;
				~Guard() { if (s) s->DeRegisterExternalTaskThread(); }
			} guard;
			guard.s = &pImpl->scheduler;
		}

		auto task = std::make_unique<enki::TaskSet>(1,
			[f = std::move(func)](enki::TaskSetPartition, uint32_t) {
				f();
			}
		);

		task->m_Priority = static_cast<enki::TaskPriority>(static_cast<uint32_t>(priority));

		enki::TaskSet* rawTask = task.get();
		{
			std::lock_guard<std::mutex> lock(pImpl->pendingMutex);
			pImpl->collectCompleted();
			pImpl->pendingTasks.push_back(std::move(task));
			pImpl->scheduler.AddTaskSetToPipe(rawTask);
		}
	}

}
