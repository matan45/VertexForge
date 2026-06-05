#include "../print/Log.hpp"
#include "TaskGraphImpl.hpp"

namespace threading {

	TaskGraph::TaskGraph() : pImpl(std::make_unique<Impl>()) {}
	TaskGraph::~TaskGraph() = default;
	TaskGraph::TaskGraph(TaskGraph&&) noexcept = default;
	TaskGraph& TaskGraph::operator=(TaskGraph&&) noexcept = default;

	void TaskGraph::execute(bool profilingEnabled)
	{
		if (!pImpl->scheduler || pImpl->nodes.empty()) return;

		if (profilingEnabled) {
			pImpl->baseTime = std::chrono::high_resolution_clock::now();

			for (auto& entry : pImpl->profileData) {
				entry.startTimeNs = 0;
				entry.endTimeNs = 0;
				entry.threadId = 0;
			}
		}

		auto* baseTimePtr = &pImpl->baseTime;

		// Execute cached layers (computed once at build time)
		for (size_t layerIdx = 0; layerIdx < pImpl->layers.size(); ++layerIdx) {
			auto& layer = pImpl->layers[layerIdx];

			if (layer.size() == 1) {
				// Single task - execute directly on this thread
				uint32_t idx = layer[0];
				auto& node = pImpl->nodes[idx];

				if (profilingEnabled) {
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
					node.fn();
				}
			}
			else {
				// Multiple tasks in this layer. Honor the `pinned` flag:
				// non-pinned tasks go to enkiTS worker threads; pinned tasks
				// run on the calling (main) thread sequentially. We dispatch
				// the worker tasks first so they execute in parallel with
				// the pinned-task work, then wait for them.
				std::vector<uint32_t> workerIndices;
				std::vector<uint32_t> pinnedIndices;
				workerIndices.reserve(layer.size());
				pinnedIndices.reserve(layer.size());
				for (uint32_t idx : layer) {
					if (pImpl->nodes[idx].pinned) pinnedIndices.push_back(idx);
					else workerIndices.push_back(idx);
				}

				std::vector<std::unique_ptr<enki::TaskSet>> taskSets(workerIndices.size());
				for (size_t t = 0; t < workerIndices.size(); ++t) {
					uint32_t idx = workerIndices[t];
					auto& node = pImpl->nodes[idx];

					if (profilingEnabled) {
						auto* entryPtr = &pImpl->profileData[idx];
						taskSets[t] = std::make_unique<enki::TaskSet>(1,
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
					}
					else {
						taskSets[t] = std::make_unique<enki::TaskSet>(1,
							[&fn = node.fn](enki::TaskSetPartition, uint32_t) {
							fn();
							}
						);
					}
					taskSets[t]->m_Priority = static_cast<enki::TaskPriority>(
						static_cast<uint32_t>(node.priority));
				}

				for (auto& task : taskSets) {
					pImpl->scheduler->AddTaskSetToPipe(task.get());
				}

				// Run pinned tasks on the calling thread while workers execute.
				for (uint32_t idx : pinnedIndices) {
					auto& node = pImpl->nodes[idx];
					if (profilingEnabled) {
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
						node.fn();
					}
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
