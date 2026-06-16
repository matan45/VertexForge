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

		// Published to the cached task lambdas (read on worker threads). Set before any
		// AddTaskSetToPipe so the scheduler's pipe atomics establish the happens-before.
		pImpl->profilingEnabled = profilingEnabled;

		if (profilingEnabled) {
			pImpl->baseTime = std::chrono::high_resolution_clock::now();

			for (auto& entry : pImpl->profileData) {
				entry.startTimeNs = 0;
				entry.endTimeNs = 0;
				entry.threadId = 0;
			}
		}

		auto* baseTimePtr = &pImpl->baseTime;

		// Runs a node's function inline on the calling (main) thread, with optional timing.
		auto runInline = [&](uint32_t idx) {
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
		};

		// Execute cached layers (computed once at build time)
		for (auto& layer : pImpl->layers) {
			if (layer.size() == 1) {
				// Single task - execute directly on this thread (pinned or not).
				runInline(layer[0]);
				continue;
			}

			// Multiple tasks in this layer. Non-pinned tasks go to enkiTS worker
			// threads via their pre-built (cached) TaskSet; pinned tasks run on the
			// calling thread. Dispatch the workers first so they run in parallel
			// with the pinned-task work, then wait for them.
			auto& dispatched = pImpl->dispatchScratch;
			dispatched.clear();
			for (uint32_t idx : layer) {
				if (pImpl->nodes[idx].pinned) continue;
				enki::TaskSet* task = pImpl->cachedTaskSets[idx].get();
				pImpl->scheduler->AddTaskSetToPipe(task);
				dispatched.push_back(task);
			}

			// Run pinned tasks on the calling thread while workers execute.
			for (uint32_t idx : layer) {
				if (pImpl->nodes[idx].pinned) runInline(idx);
			}

			for (enki::TaskSet* task : dispatched) {
				pImpl->scheduler->WaitforTask(task);
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
