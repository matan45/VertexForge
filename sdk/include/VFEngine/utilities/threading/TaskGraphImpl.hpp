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

	// Wait target for a frame. Depends on every leaf node, so WaitforTask(&terminal)
	// returns once the entire graph has completed this frame. A plain ICompletable needs
	// no override: its base OnDependenciesComplete clears the running count on completion.
	struct TerminalTask : enki::ICompletable {};

	struct TaskGraph::Impl {
		enki::TaskScheduler* scheduler = nullptr;

		// Graph definition (immutable after build)
		std::vector<TaskNodeInfo> nodes;
		std::vector<TaskEdgeInfo> edges;
		std::vector<std::string> taskNames;            // cached for queries
		std::vector<std::vector<uint32_t>> adjacency;  // adjacency[i] = dependents of task i
		std::vector<uint32_t> rootIndices;              // nodes with in-degree 0
		std::vector<uint32_t> leafIndices;              // nodes with out-degree 0

		// Read per-frame by the cached task lambdas to toggle timing capture.
		bool profilingEnabled = false;

		// Profiling data - one entry per task (reused across frames)
		std::vector<TaskProfileEntry> profileData;
		std::chrono::high_resolution_clock::time_point baseTime;

		// Persistent enkiTS completables, built once and re-armed every frame (roots are
		// re-added in execute(); the rest auto-launch via native dependencies). Each node
		// uses exactly one kind: non-pinned -> cachedTaskSets[i]; pinned (main thread) ->
		// cachedPinnedTasks[i]. The opposite-kind slot stays null.
		std::vector<std::unique_ptr<enki::TaskSet>> cachedTaskSets;
		std::vector<std::unique_ptr<enki::LambdaPinnedTask>> cachedPinnedTasks;

		// Single wait target depending on all leaves (see TerminalTask).
		TerminalTask terminal;

		// enki::Dependency storage. MUST be declared last so it is destroyed FIRST (reverse
		// member-destruction order): ~Dependency dereferences its dependent task and unwires
		// from its dependency task, so every completable it references (the task sets, the
		// pinned tasks, and `terminal` above) must still be alive at that point. Sized once at
		// build() and never resized again, so the intrusive Dependency pointers stay stable.
		std::vector<std::vector<enki::Dependency>> nodeDeps; // nodeDeps[i] = incoming edges of node i
		std::vector<enki::Dependency> terminalDeps;          // terminal's dependencies on the leaves
	};

}
