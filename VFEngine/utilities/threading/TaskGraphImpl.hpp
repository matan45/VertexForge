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

		// Profiling data - one entry per task (reused across frames)
		std::vector<TaskProfileEntry> profileData;
		std::chrono::high_resolution_clock::time_point baseTime;
	};

}
