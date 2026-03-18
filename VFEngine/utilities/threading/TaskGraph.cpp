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

		// Compute topological layers: tasks at the same layer have all dependencies
		// in earlier layers and can execute in parallel.
		std::vector<uint32_t> inDegree(nodeCount, 0);
		std::vector<std::vector<uint32_t>> reverseAdj(nodeCount); // reverseAdj[i] = tasks i depends on

		for (auto& edge : pImpl->edges) {
			inDegree[edge.to]++;
			reverseAdj[edge.to].push_back(edge.from);
		}

		// Build layers via BFS (Kahn's algorithm layer by layer)
		std::vector<std::vector<uint32_t>> layers;
		std::vector<uint32_t> currentLayer;

		for (uint32_t i = 0; i < nodeCount; ++i) {
			if (inDegree[i] == 0) {
				currentLayer.push_back(i);
			}
		}

		std::vector<uint32_t> tempInDegree = inDegree;

		while (!currentLayer.empty()) {
			layers.push_back(currentLayer);
			std::vector<uint32_t> nextLayer;

			for (uint32_t node : currentLayer) {
				for (uint32_t dep : pImpl->adjacency[node]) {
					if (--tempInDegree[dep] == 0) {
						nextLayer.push_back(dep);
					}
				}
			}

			currentLayer = std::move(nextLayer);
		}

		// Execute each layer: tasks within a layer run in parallel
		auto* baseTimePtr = &pImpl->baseTime;

		for (auto& layer : layers) {
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
				// Multiple tasks - dispatch in parallel via enkiTS
				std::vector<std::unique_ptr<enki::TaskSet>> taskSets(layer.size());

				for (size_t t = 0; t < layer.size(); ++t) {
					uint32_t idx = layer[t];
					auto& node = pImpl->nodes[idx];
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
					taskSets[t]->m_Priority = static_cast<enki::TaskPriority>(
						static_cast<uint32_t>(node.priority));
				}

				// Add all tasks to pipe
				for (auto& task : taskSets) {
					pImpl->scheduler->AddTaskSetToPipe(task.get());
				}

				// Wait for all tasks in this layer to complete
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
