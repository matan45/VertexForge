#pragma once
#include "threading/TaskGraph.hpp"
#include "threading/TaskGraphBuilder.hpp"
#include "threading/TaskProfiler.hpp"

#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace services {

	struct FrameTask {
		std::string name;
		std::function<void()> fn;
		threading::JobPriority priority = threading::JobPriority::NORMAL;
		bool mainThread = false;
	};

	struct FrameTaskDependency {
		std::string dependent;
		std::string dependency;
	};

	class FrameTaskGraph {
	public:
		FrameTaskGraph();
		~FrameTaskGraph();

		// Add a task to the frame graph
		void addTask(const std::string& name, std::function<void()> fn,
			threading::JobPriority priority = threading::JobPriority::NORMAL,
			bool mainThread = false);

		// Declare dependency: dependent runs after dependency
		void addDependency(const std::string& dependent, const std::string& dependency);

		// Build the graph from the registered tasks and dependencies.
		// Must be called after all addTask/addDependency calls.
		bool compile();

		// Execute the frame graph (blocks until all tasks complete)
		void execute();

		// Get the latest profile data
		const threading::FrameProfileSnapshot& getLatestProfile() const;

		// Get the graph structure for visualization
		const std::vector<std::string>& getTaskNames() const;
		const std::vector<std::vector<uint32_t>>& getAdjacency() const;

		// Register CQRS event handlers for editor queries
		void registerEventHandlers();
		void unregisterEventHandlers();

	private:
		std::vector<FrameTask> tasks;
		std::vector<FrameTaskDependency> dependencies;

		std::unique_ptr<threading::TaskGraph> graph;
		threading::FrameProfileSnapshot latestProfile;

		// Cached for structure queries
		std::vector<std::string> cachedNames;
		std::vector<std::vector<uint32_t>> cachedAdjacency;
	};

}
