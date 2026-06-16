#pragma once
#include "TaskGraph.hpp"

#include <TaskScheduler.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace threading {

	struct TaskNodeInfo {
		std::string name;
		std::function<void()> fn;
		JobPriority priority = JobPriority::NORMAL;
		bool pinned = false;
	};

	struct TaskEdgeInfo {
		uint32_t from; // dependency (must complete first)
		uint32_t to;   // dependent (runs after 'from')
	};

	struct TaskGraph::Impl {
		enki::TaskScheduler* scheduler = nullptr;

		// Graph definition (immutable after build)
		std::vector<TaskNodeInfo> nodes;
		std::vector<TaskEdgeInfo> edges;
		std::vector<std::string> taskNames;            // cached for queries
		std::vector<std::vector<uint32_t>> adjacency;  // adjacency[i] = dependents of task i
		std::vector<uint32_t> rootIndices;              // nodes with in-degree 0
		std::vector<uint32_t> leafIndices;              // nodes with out-degree 0

		// Cached topological layers (computed once at build, reused every frame)
		std::vector<std::vector<uint32_t>> layers;

		// Persistent enkiTS task object per non-pinned node, built once and re-added
		// to the pipe every frame (pinned-node slots stay null - they run inline on
		// the main thread). Avoids per-frame TaskSet/vector heap allocation.
		std::vector<std::unique_ptr<enki::TaskSet>> cachedTaskSets;

		// Scratch list of the worker tasks dispatched in the current layer. Reused
		// every frame (cleared, capacity retained); execute() is single-threaded.
		std::vector<enki::TaskSet*> dispatchScratch;

		// Read per-frame by the cached task lambdas to toggle timing capture.
		bool profilingEnabled = false;

		// Profiling data - one entry per task (reused across frames)
		std::vector<TaskProfileEntry> profileData;
		std::chrono::high_resolution_clock::time_point baseTime;
	};

}
