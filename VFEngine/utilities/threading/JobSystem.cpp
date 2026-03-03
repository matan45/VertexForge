#include "JobSystem.hpp"
#include "../print/EditorLogger.hpp"

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

	void JobSystem::init(uint32_t threadCount)
	{
		if (threadCount == 0) {
			uint32_t hw = std::thread::hardware_concurrency();
			threadCount = (hw > 3) ? (hw - 2) : 2;
		}

		pImpl->scheduler.Initialize(threadCount);
		vfLogInfo("JobSystem initialized with {} threads", pImpl->scheduler.GetNumTaskThreads());
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
		}

		pImpl->scheduler.AddTaskSetToPipe(rawTask);
	}

}
