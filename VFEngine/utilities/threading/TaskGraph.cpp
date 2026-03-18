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

		uint32_t nodeCount = static_cast<uint32_t>(pImpl->nodes.size());

		// Record base time for profiling
		pImpl->baseTime = std::chrono::high_resolution_clock::now();

		// Reset profile entries
		for (auto& entry : pImpl->profileData) {
			entry.startTimeNs = 0;
			entry.endTimeNs = 0;
			entry.threadId = 0;
		}

		// Rebuild enkiTS objects each frame (enkiTS Dependency state is not resettable)
		std::vector<std::unique_ptr<enki::TaskSet>> taskSets(nodeCount);
		std::vector<std::unique_ptr<enki::LambdaPinnedTask>> pinnedTasks(nodeCount);

		auto* baseTimePtr = &pImpl->baseTime;

		for (uint32_t i = 0; i < nodeCount; ++i) {
			auto& node = pImpl->nodes[i];
			auto* entryPtr = &pImpl->profileData[i];

			if (node.pinned) {
				pinnedTasks[i] = std::make_unique<enki::LambdaPinnedTask>(
					0u,
					[&fn = node.fn, entryPtr, baseTimePtr]() {
						auto start = std::chrono::high_resolution_clock::now();
						fn();
						auto end = std::chrono::high_resolution_clock::now();
						entryPtr->startTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(start - *baseTimePtr).count());
						entryPtr->endTimeNs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::nanoseconds>(end - *baseTimePtr).count());
						entryPtr->threadId = 0;
					}
				);
				pinnedTasks[i]->m_Priority = static_cast<enki::TaskPriority>(
					static_cast<uint32_t>(node.priority));
			}
			else {
				taskSets[i] = std::make_unique<enki::TaskSet>(1,
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
				taskSets[i]->m_Priority = static_cast<enki::TaskPriority>(
					static_cast<uint32_t>(node.priority));
			}
		}

		// Wire enkiTS dependencies
		std::vector<enki::Dependency> dependencies(pImpl->edges.size());
		for (size_t e = 0; e < pImpl->edges.size(); ++e) {
			auto& edge = pImpl->edges[e];

			enki::ICompletable* source = pImpl->nodes[edge.from].pinned
				? static_cast<enki::ICompletable*>(pinnedTasks[edge.from].get())
				: static_cast<enki::ICompletable*>(taskSets[edge.from].get());

			enki::ICompletable* target = pImpl->nodes[edge.to].pinned
				? static_cast<enki::ICompletable*>(pinnedTasks[edge.to].get())
				: static_cast<enki::ICompletable*>(taskSets[edge.to].get());

			target->SetDependency(dependencies[e], source);
		}

		// Completion sentinel depends on all leaf tasks
		enki::TaskSet completionTask(1, [](enki::TaskSetPartition, uint32_t) {});
		std::vector<enki::Dependency> completionDeps(pImpl->leafIndices.size());
		for (size_t l = 0; l < pImpl->leafIndices.size(); ++l) {
			uint32_t leafIdx = pImpl->leafIndices[l];
			enki::ICompletable* leaf = pImpl->nodes[leafIdx].pinned
				? static_cast<enki::ICompletable*>(pinnedTasks[leafIdx].get())
				: static_cast<enki::ICompletable*>(taskSets[leafIdx].get());

			completionTask.SetDependency(completionDeps[l], leaf);
		}

		// Add root tasks to the scheduler
		for (uint32_t idx : pImpl->rootIndices) {
			if (pImpl->nodes[idx].pinned) {
				pImpl->scheduler->AddPinnedTask(pinnedTasks[idx].get());
			}
			else {
				pImpl->scheduler->AddTaskSetToPipe(taskSets[idx].get());
			}
		}

		// Wait for completion (main thread participates in work stealing + pinned tasks)
		pImpl->scheduler->WaitforTask(&completionTask);
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
