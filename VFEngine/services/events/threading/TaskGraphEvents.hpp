#pragma once
#include "../EventTypes.hpp"
#include "threading/TaskProfiler.hpp"

#include <vector>
#include <string>

namespace events::threading {

	struct TaskGraphStructure {
		std::vector<std::string> taskNames;
		std::vector<std::vector<uint32_t>> adjacency;
	};

	struct GetTaskGraphProfileQuery : IQuery<::threading::FrameProfileSnapshot> {
		std::string_view getName() const override { return "GetTaskGraphProfileQuery"; }
	};

	struct GetTaskGraphStatsQuery : IQuery<std::vector<::threading::TaskProfileStats>> {
		std::string_view getName() const override { return "GetTaskGraphStatsQuery"; }
	};

	struct GetTaskGraphStructureQuery : IQuery<TaskGraphStructure> {
		std::string_view getName() const override { return "GetTaskGraphStructureQuery"; }
	};

	struct SetTaskGraphProfilingEnabledCommand : ICommand<> {
		bool enabled = false;
		std::string_view getName() const override { return "SetTaskGraphProfilingEnabledCommand"; }
	};

	struct IsTaskGraphProfilingEnabledQuery : IQuery<bool> {
		std::string_view getName() const override { return "IsTaskGraphProfilingEnabledQuery"; }
	};

}
