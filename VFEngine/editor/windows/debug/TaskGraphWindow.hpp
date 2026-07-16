#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "threading/TaskProfiler.hpp"
#include "stats/FrameDrawStats.hpp" // render::DrawCategory / FrameDrawStats::kCount
#include "stats/GpuPassStats.hpp"   // render::GpuFrameStats sink (published by graphics)
#include "resource/ResourceLoadScheduler.hpp" // ActiveLoadInfo / CompletedLoadRecord

#include <array>
#include <vector>
#include <string>
#include <unordered_map>

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

		// Per-frame draw-call breakdown (VK-1370), refreshed alongside the task stats.
		uint32_t drawCallTotal = 0;
		std::array<uint32_t, render::FrameDrawStats::kCount> drawCallsByCategory = {};

		// CPU viewport frame totals (ms) from the TaskProfiler history ring,
		// oldest-to-newest, for the Timeline tab's history plot.
		std::vector<float> cpuHistoryMs;

		// GPU pass timings published by the render graph profiler.
		bool gpuProfilingEnabled = false;
		// Mirrors the request we last pushed to the sink so draw() can spot the
		// edge and stop per-pass capture when the window closes. Not merged with
		// `visible` or `gpuProfilingEnabled`: ImGui's close button writes `visible`
		// behind our back, so the wanted state is (visible && gpuProfilingEnabled)
		// and only this remembers what we actually asked for.
		bool gpuCaptureActive = false;
		// Set by showGpuTab() so the Debug > GPU Profiler entry lands on the right
		// tab; consumed (and cleared) by the next draw.
		bool selectGpuTab = false;
		render::GpuFrameStats gpuStats;
		std::vector<float> gpuHistoryMs;

		// Resource scheduler loads (always collected — a few timestamps per
		// load, no toggle needed).
		resource::SchedulerStats loadStats;
		std::vector<resource::ActiveLoadInfo> activeLoads;
		std::vector<resource::CompletedLoadRecord> recentLoads;
		std::unordered_map<asset::AssetGUID, std::string, asset::AssetGUID::Hash> loadNameCache;

	public:
		TaskGraphWindow() = default;
		~TaskGraphWindow() override = default;

		void draw() override;
		void show() { visible = true; }
		// Opens the window straight on the GPU Passes tab — the per-pass GPU
		// profiler is otherwise undiscoverable behind a tab of "Task Graph".
		void showGpuTab()
		{
			visible = true;
			selectGpuTab = true;
			gpuProfilingEnabled = true;
		}

	private:
		void refreshData();
		void drawTimeline();
		void drawStatistics();
		void drawDAG();
		void drawDrawCalls();
		void drawGpuPasses();
		// Shared by the graph-pass and sub-scope tables. `rows` indexes into
		// gpuStats.passTimings; `denomMs` is the share-of-frame denominator.
		void drawGpuPassTable(const char* id, const std::vector<size_t>& rows, float denomMs);
		void drawLoading();
		std::string loadDisplayName(const asset::AssetGUID& guid, const std::string& debugName);
	};
}
