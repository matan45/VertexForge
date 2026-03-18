#include "../print/Log.hpp"
#include "TaskGraphImpl.hpp"

namespace threading {

	TaskGraph::TaskGraph() : pImpl(std::make_unique<Impl>()) {}
	TaskGraph::~TaskGraph() = default;
	TaskGraph::TaskGraph(TaskGraph&&) noexcept = default;
	TaskGraph& TaskGraph::operator=(TaskGraph&&) noexcept = default;

	void TaskGraph::execute()
	{
		if (!pImpl->scheduler || pImpl->nodes.empty()) return;

		// Record base time for profiling
		pImpl->baseTime = std::chrono::high_resolution_clock::now();

		// Reset profile entries
		for (auto& entry : pImpl->profileData) {
			entry.startTimeNs = 0;
			entry.endTimeNs = 0;
			entry.threadId = 0;
		}

		auto* baseTimePtr = &pImpl->baseTime;

		// Execute cached layers (computed once at build time)
		for (size_t layerIdx = 0; layerIdx < pImpl->layers.size(); ++layerIdx) {
			auto& layer = pImpl->layers[layerIdx];

			if (layer.size() == 1) {
				// Single task - execute directly on this thread
				uint32_t idx = layer[0];
				auto& node = pImpl->nodes[idx];
				auto* entryPtr = &pImpl->profileData[idx];

				auto start = std::chrono::high_resolution_clock::now();
				node.fn();
				auto end = std::chrono::high_resolution_clock::now();

				entryPtr->startTimeNs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(start - *baseTimePtr).count());
				entryPtr->endTimeNs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::nanoseconds>(end - *baseTimePtr).count());
				entryPtr->threadId = 0;
			}
			else {
				// Multiple tasks - reuse pre-allocated TaskSets
				auto& taskSets = pImpl->layerTaskSets[layerIdx];

				for (size_t t = 0; t < layer.size(); ++t) {
					uint32_t idx = layer[t];
					auto& node = pImpl->nodes[idx];
					auto* entryPtr = &pImpl->profileData[idx];

					// Reconstruct in-place (TaskSet has deleted operator=  due to atomic members)
					taskSets[t]->~TaskSet();
					new (taskSets[t].get()) enki::TaskSet(1,
						[&fn = node.fn, entryPtr, baseTimePtr](
							enki::TaskSetPartition, uint32_t threadNum) {
							auto start = std::chrono::high_resolution_clock::now();
							fn();
							auto end = std::chrono::high_resolution_clock::now();
							entryPtr->startTimeNs = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::nanoseconds>(start - *baseTimePtr).count());
							entryPtr->endTimeNs = static_cast<uint64_t>(
								std::chrono::duration_cast<std::chrono::nanoseconds>(end - *baseTimePtr).count());
							entryPtr->threadId = threadNum;
						}
					);
					taskSets[t]->m_Priority = static_cast<enki::TaskPriority>(
						static_cast<uint32_t>(node.priority));
				}

				for (auto& task : taskSets) {
					pImpl->scheduler->AddTaskSetToPipe(task.get());
				}
				for (auto& task : taskSets) {
					pImpl->scheduler->WaitforTask(task.get());
				}
			}
		}
	}

	const std::vector<TaskProfileEntry>& TaskGraph::getProfileData() const
	{
		return pImpl->profileData;
	}

	const std::vector<std::string>& TaskGraph::getTaskNames() const
	{
		return pImpl->taskNames;
	}

	size_t TaskGraph::getTaskCount() const
	{
		return pImpl->nodes.size();
	}

	const std::vector<std::vector<uint32_t>>& TaskGraph::getAdjacency() const
	{
		return pImpl->adjacency;
	}

}
