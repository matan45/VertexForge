#include "../print/Log.hpp"
#include "TaskGraphImpl.hpp"

#include <cassert>

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

		// Zero the timing fields every frame regardless of profiling state. Otherwise a
		// profiled frame followed by an unprofiled one would leave getProfileData()
		// returning last-profiled-frame timings as if they were current (the cached
		// lambdas skip the capture branch when profiling is off and never overwrite them).
		for (auto& entry : pImpl->profileData) {
			entry.startTimeNs = 0;
			entry.endTimeNs = 0;
			entry.threadId = 0;
		}

		// The previous frame's WaitforTask below fully drained the graph, so the terminal
		// (and every node) is complete on entry. Catches a not-drained / re-entrant misuse;
		// execute() is contractually main-thread-only.
		assert(pImpl->terminal.GetIsComplete());

		// Kick the roots (in-degree 0). enkiTS arms the whole reachable graph and auto-launches
		// each downstream node the moment all its predecessors finish (native dependencies), so
		// there is no per-layer barrier and independent work overlaps freely across layers.
		for (uint32_t idx : pImpl->rootIndices) {
			if (pImpl->nodes[idx].pinned) {
				pImpl->scheduler->AddPinnedTask(pImpl->cachedPinnedTasks[idx].get());
			}
			else {
				pImpl->scheduler->AddTaskSetToPipe(pImpl->cachedTaskSets[idx].get());
			}
		}

		// Block until the terminal sentinel (depends on every leaf) completes. While waiting,
		// this (main) thread runs its own thread-0 pinned tasks and helps run worker tasks.
		pImpl->scheduler->WaitforTask(&pImpl->terminal);
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
