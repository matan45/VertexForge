#pragma once
#include "JobSystem.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

namespace enki {
	class TaskScheduler;
	class ICompletable;
	class Dependency;
}

namespace threading {

	using TaskHandle = uint32_t;
	static constexpr TaskHandle INVALID_TASK_HANDLE = ~0u;

	struct TaskProfileEntry {
		std::string name;
		uint32_t threadId = 0;
		uint64_t startTimeNs = 0;
		uint64_t endTimeNs = 0;
		JobPriority priority = JobPriority::NORMAL;
	};

	class TaskGraph {
	public:
		~TaskGraph();
		TaskGraph(TaskGraph&&) noexcept;
		TaskGraph& operator=(TaskGraph&&) noexcept;

		// Execute the entire graph, blocking until all tasks complete.
		// Must be called from the main thread (thread that initialized JobSystem).
		void execute(bool profilingEnabled = true);

		const std::vector<TaskProfileEntry>& getProfileData() const;
		const std::vector<std::string>& getTaskNames() const;
		size_t getTaskCount() const;

		// Adjacency list: adjacency[i] contains indices of tasks that depend on task i
		const std::vector<std::vector<uint32_t>>& getAdjacency() const;

	private:
		friend class TaskGraphBuilder;
		TaskGraph();

		struct Impl;
		std::unique_ptr<Impl> pImpl;
	};

}
