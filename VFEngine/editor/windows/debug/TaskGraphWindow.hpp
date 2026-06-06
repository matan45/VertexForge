#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "threading/TaskProfiler.hpp"

#include <vector>
#include <string>

namespace windows
{
	class TaskGraphWindow : public controllers::imguiHandler::ImguiWindow
	{
	private:
		bool visible = false;
		bool paused = false;
		bool profilingEnabled = false;
		float refreshTimer = 0.0f;
		static constexpr float REFRESH_INTERVAL = 0.5f;

		// Cached data
		threading::FrameProfileSnapshot latestFrame;
		std::vector<threading::TaskProfileStats> stats;
		std::vector<std::string> taskNames;
		std::vector<std::vector<uint32_t>> adjacency;
		uint32_t maxThreadId = 0;

	public:
		TaskGraphWindow() = default;
		~TaskGraphWindow() override = default;

		void draw() override;
		void show() { visible = true; }

	private:
		void refreshData();
		void drawTimeline();
		void drawStatistics();
		void drawDAG();
	};
}
