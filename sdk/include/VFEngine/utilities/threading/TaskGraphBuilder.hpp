#pragma once
#include "TaskGraph.hpp"

#include <string_view>

namespace threading {

#pragma warning(push)
#pragma warning(disable: 4251) // std::unique_ptr<BuilderImpl> member crossing the DLL boundary
	class VF_THREADING_API TaskGraphBuilder {
	public:
		TaskGraphBuilder();
		~TaskGraphBuilder();

		// Add a named task. Returns a handle used in depends()/parallel().
		TaskHandle task(std::string_view name, std::function<void()> fn,
			JobPriority priority = JobPriority::NORMAL);

		// Add a named task pinned to the main thread (thread 0).
		// Use for GLFW/input calls that must run on the main thread.
		TaskHandle pinnedTask(std::string_view name, std::function<void()> fn,
			JobPriority priority = JobPriority::NORMAL);

		// Declare that 'dependent' must run after 'dependency' completes.
		TaskGraphBuilder& depends(TaskHandle dependent, TaskHandle dependency);

		// Validate the DAG (cycle detection) and compile into an executable TaskGraph.
		// Returns nullptr if cycles are detected.
		std::unique_ptr<TaskGraph> build();

	private:
		struct BuilderImpl;
		std::unique_ptr<BuilderImpl> pImpl;
	};
#pragma warning(pop)

}
