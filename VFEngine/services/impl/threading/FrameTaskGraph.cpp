#include "print/Log.hpp"
#include "FrameTaskGraph.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/threading/TaskGraphEvents.hpp"

#include <unordered_map>

namespace services {

	FrameTaskGraph::FrameTaskGraph() = default;
	FrameTaskGraph::~FrameTaskGraph() = default;

	void FrameTaskGraph::addTask(const std::string& name, std::function<void()> fn,
		threading::JobPriority priority, bool mainThread)
	{
		tasks.push_back({ name, std::move(fn), priority, mainThread });
	}

	void FrameTaskGraph::addDependency(const std::string& dependent, const std::string& dependency)
	{
		dependencies.push_back({ dependent, dependency });
	}

	bool FrameTaskGraph::compile()
	{
		threading::TaskGraphBuilder builder;

		// Map task names to handles
		std::unordered_map<std::string, threading::TaskHandle> handleMap;

		for (auto& task : tasks) {
			threading::TaskHandle handle;
			if (task.mainThread) {
				handle = builder.pinnedTask(task.name, task.fn, task.priority);
			}
			else {
				handle = builder.task(task.name, task.fn, task.priority);
			}
			handleMap[task.name] = handle;
		}

		// Wire dependencies
		for (auto& dep : dependencies) {
			auto depIt = handleMap.find(dep.dependent);
			auto reqIt = handleMap.find(dep.dependency);

			if (depIt == handleMap.end()) {
				vfLogWarning("[FrameTaskGraph] Unknown dependent task: '{}'", dep.dependent);
				continue;
			}
			if (reqIt == handleMap.end()) {
				vfLogWarning("[FrameTaskGraph] Unknown dependency task: '{}'", dep.dependency);
				continue;
			}

			builder.depends(depIt->second, reqIt->second);
		}

		graph = builder.build();
		if (!graph) {
			vfLogError("[FrameTaskGraph] Failed to compile frame task graph");
			return false;
		}

		// Cache structure for queries
		cachedNames = graph->getTaskNames();
		cachedAdjacency = graph->getAdjacency();

		vfLogInfo("[FrameTaskGraph] Compiled with {} tasks", graph->getTaskCount());
		return true;
	}

	void FrameTaskGraph::execute()
	{
		if (!graph) return;

		graph->execute();

		// Record profiling data (TaskProfiler computes frameDurationNs)
		auto& profileData = graph->getProfileData();
		threading::TaskProfiler::instance().recordFrame(profileData);

		// Use TaskProfiler as single source of truth for frame duration
		latestProfile = threading::TaskProfiler::instance().getLatestFrame();
	}

	const threading::FrameProfileSnapshot& FrameTaskGraph::getLatestProfile() const
	{
		return latestProfile;
	}

	const std::vector<std::string>& FrameTaskGraph::getTaskNames() const
	{
		return cachedNames;
	}

	const std::vector<std::vector<uint32_t>>& FrameTaskGraph::getAdjacency() const
	{
		return cachedAdjacency;
	}

	void FrameTaskGraph::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerQueryHandler<events::threading::GetTaskGraphProfileQuery>(
			[this](const events::threading::GetTaskGraphProfileQuery&) {
				return latestProfile;
			}
		);

		dispatcher.registerQueryHandler<events::threading::GetTaskGraphStatsQuery>(
			[](const events::threading::GetTaskGraphStatsQuery&) {
				return threading::TaskProfiler::instance().computeStats();
			}
		);

		dispatcher.registerQueryHandler<events::threading::GetTaskGraphStructureQuery>(
			[this](const events::threading::GetTaskGraphStructureQuery&) {
				events::threading::TaskGraphStructure structure;
				structure.taskNames = cachedNames;
				structure.adjacency = cachedAdjacency;
				return structure;
			}
		);
	}

	void FrameTaskGraph::unregisterEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unregisterQueryHandler<events::threading::GetTaskGraphProfileQuery>();
		dispatcher.unregisterQueryHandler<events::threading::GetTaskGraphStatsQuery>();
		dispatcher.unregisterQueryHandler<events::threading::GetTaskGraphStructureQuery>();
	}

}
