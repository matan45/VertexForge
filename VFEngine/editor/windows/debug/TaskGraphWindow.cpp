#include "TaskGraphWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/threading/TaskGraphEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "stats/FrameHistoryMath.hpp"
#include "imgui.h"
#include "print/Log.hpp"

#include <filesystem>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace windows
{
	// Frame-time history plot with median-based hitch flagging, shared by
	// the CPU (Timeline tab) and GPU (GPU Passes tab) sections.
	static void drawFrameHistoryPlot(const char* label, const std::vector<float>& historyMs)
	{
		if (historyMs.size() < 4)
		{
			ImGui::TextDisabled("Collecting history...");
			return;
		}

		float maxMs = *std::max_element(historyMs.begin(), historyMs.end());
		float medianMs = render::history::median(historyMs);
		auto hitches = render::history::findHitches(historyMs);

		char overlay[64];
		snprintf(overlay, sizeof(overlay), "%.2f ms (median %.2f)", historyMs.back(), medianMs);
		ImGui::PlotLines(label, historyMs.data(), static_cast<int>(historyMs.size()), 0,
			overlay, 0.0f, maxMs * 1.1f, ImVec2(-1.0f, 60.0f));

		if (hitches.empty())
		{
			ImGui::TextDisabled("No hitches in the last %zu frames (>2x median)", historyMs.size());
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
				"%zu hitch frame(s) in the last %zu (>2x median, peak %.2f ms)",
				hitches.size(), historyMs.size(), maxMs);
		}
	}

	// Color palette for task bars (distinguishable colors)
	static ImU32 getTaskColor(uint32_t index)
	{
		static const ImU32 colors[] = {
			IM_COL32(66, 133, 244, 220),   // blue
			IM_COL32(234, 67, 53, 220),    // red
			IM_COL32(251, 188, 4, 220),    // yellow
			IM_COL32(52, 168, 83, 220),    // green
			IM_COL32(171, 71, 188, 220),   // purple
			IM_COL32(255, 112, 67, 220),   // orange
			IM_COL32(0, 172, 193, 220),    // cyan
			IM_COL32(124, 179, 66, 220),   // lime
			IM_COL32(233, 30, 99, 220),    // pink
			IM_COL32(63, 81, 181, 220),    // indigo
			IM_COL32(255, 152, 0, 220),    // amber
			IM_COL32(0, 150, 136, 220),    // teal
		};
		return colors[index % (sizeof(colors) / sizeof(colors[0]))];
	}

	void TaskGraphWindow::draw()
	{
		if (!visible) return;

		if (!paused)
		{
			refreshTimer += ImGui::GetIO().DeltaTime;
			if (refreshTimer >= REFRESH_INTERVAL)
			{
				refreshData();
				refreshTimer = 0.0f;
			}
		}

		ImGui::SetNextWindowSize(ImVec2(700, 450), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Task Graph Profiler", &visible))
		{
			// Toolbar
			bool enabled = profilingEnabled;
			if (ImGui::Checkbox("Profiling", &enabled))
			{
				profilingEnabled = enabled;
				events::threading::SetTaskGraphProfilingEnabledCommand cmd;
				cmd.enabled = profilingEnabled;
				try
				{
					events::EventDispatcher::instance().execute(cmd);
				}
				catch (const std::exception&)
				{
				}

				if (!profilingEnabled)
				{
					latestFrame = {};
					stats.clear();
					maxThreadId = 0;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(paused ? "Resume" : "Pause"))
			{
				paused = !paused;
			}
			ImGui::SameLine();
			if (paused)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "PAUSED");
			}
			else
			{
				if (profilingEnabled)
				{
					float viewportMs = static_cast<float>(threading::viewportFrameDurationNs(latestFrame)) / 1e6f;
					float imguiMs = static_cast<float>(threading::taskDurationNs(latestFrame, "ImGuiDraw")) / 1e6f;
					ImGui::Text("Viewport: %.3f ms | ImGui: %.3f ms | Tasks: %zu",
						viewportMs, imguiMs, latestFrame.entries.size());
				}
				else
					ImGui::TextDisabled("Profiling disabled");
			}

			ImGui::Separator();

			if (ImGui::BeginTabBar("TaskGraphTabs"))
			{
				if (ImGui::BeginTabItem("Timeline"))
				{
					drawTimeline();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Statistics"))
				{
					drawStatistics();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("DAG"))
				{
					drawDAG();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Draw Calls"))
				{
					drawDrawCalls();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("GPU Passes"))
				{
					drawGpuPasses();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Loading"))
				{
					drawLoading();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void TaskGraphWindow::refreshData()
	{
		try
		{
			auto& dispatcher = events::EventDispatcher::instance();

			profilingEnabled = dispatcher.query(events::threading::IsTaskGraphProfilingEnabledQuery{});

			auto structure = dispatcher.query(events::threading::GetTaskGraphStructureQuery{});
			taskNames = structure.taskNames;
			adjacency = structure.adjacency;

			// Draw-call breakdown (VK-1370) — independent of task-graph profiling.
			auto cullingStats = dispatcher.query(events::render::GetCullingStatsQuery{});
			drawCallTotal = cullingStats.totalDrawCalls;
			drawCallsByCategory = cullingStats.drawCallsByCategory;

			if (profilingEnabled)
			{
				latestFrame = dispatcher.query(events::threading::GetTaskGraphProfileQuery{});
				stats = dispatcher.query(events::threading::GetTaskGraphStatsQuery{});
				maxThreadId = threading::TaskProfiler::instance().getMaxThreadId();

				// Viewport frame totals across the history ring for the plot
				auto history = threading::TaskProfiler::instance().getHistory();
				cpuHistoryMs.clear();
				cpuHistoryMs.reserve(history.size());
				for (const auto& frame : history)
				{
					cpuHistoryMs.push_back(
						static_cast<float>(threading::viewportFrameDurationNs(frame)) / 1e6f);
				}
			}
			else
			{
				latestFrame = {};
				stats.clear();
				maxThreadId = 0;
				cpuHistoryMs.clear();
			}

			// Resource loads are always tracked by the scheduler — no toggle
			auto& loadScheduler = resource::ResourceLoadScheduler::instance();
			loadStats = loadScheduler.getStats();
			activeLoads = loadScheduler.getActiveLoads();
			recentLoads = loadScheduler.getRecentCompletions();

			// GPU pass timings flow through the GpuPassStats sink, independent
			// of CPU task-graph profiling
			auto& gpuSink = render::GpuPassStats::instance();
			gpuProfilingEnabled = gpuSink.isEnabledRequested();
			if (gpuProfilingEnabled)
			{
				gpuStats = gpuSink.snapshot();
				gpuHistoryMs = gpuSink.totalMsHistory();
			}
			else
			{
				gpuStats = {};
				gpuHistoryMs.clear();
			}
		}
		catch (const std::exception&)
		{
			// Query handlers not yet registered
		}
	}

	void TaskGraphWindow::drawTimeline()
	{
		if (latestFrame.entries.empty())
		{
			ImGui::TextDisabled("No profiling data available");
			return;
		}

		float frameDurationMs = static_cast<float>(latestFrame.frameDurationNs) / 1e6f;
		ImGui::Text("Frame duration: %.3f ms", frameDurationMs);

		drawFrameHistoryPlot("##cpuhistory", cpuHistoryMs);
		ImGui::Separator();

		// Timeline area
		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		canvasSize.y = std::max(canvasSize.y, 100.0f);

		float leftMargin = 80.0f;
		float topMargin = 20.0f;
		float rowHeight = 28.0f;
		uint32_t threadCount = maxThreadId + 1;
		float timelineWidth = canvasSize.x - leftMargin - 10.0f;

		if (latestFrame.frameDurationNs == 0) return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Background
		drawList->AddRectFilled(canvasPos,
			ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + topMargin + threadCount * rowHeight + 10.0f),
			IM_COL32(30, 30, 30, 255));

		// Thread labels
		for (uint32_t t = 0; t <= maxThreadId; ++t)
		{
			float y = canvasPos.y + topMargin + t * rowHeight;
			char label[32];
			snprintf(label, sizeof(label), "Thread %u", t);
			drawList->AddText(ImVec2(canvasPos.x + 4.0f, y + 6.0f),
				IM_COL32(180, 180, 180, 255), label);

			// Row separator
			drawList->AddLine(
				ImVec2(canvasPos.x + leftMargin, y),
				ImVec2(canvasPos.x + canvasSize.x, y),
				IM_COL32(60, 60, 60, 255));
		}

		// Time scale header
		{
			char timeLabel[32];
			snprintf(timeLabel, sizeof(timeLabel), "%.1f ms", frameDurationMs);
			drawList->AddText(ImVec2(canvasPos.x + leftMargin + timelineWidth - 50.0f, canvasPos.y + 2.0f),
				IM_COL32(180, 180, 180, 255), timeLabel);
			drawList->AddText(ImVec2(canvasPos.x + leftMargin, canvasPos.y + 2.0f),
				IM_COL32(180, 180, 180, 255), "0 ms");
		}

		// Draw task bars
		double nsToPixel = static_cast<double>(timelineWidth) / static_cast<double>(latestFrame.frameDurationNs);

		// Find earliest start for offset
		uint64_t minStart = UINT64_MAX;
		for (auto& e : latestFrame.entries)
		{
			if (e.endTimeNs > 0 && e.startTimeNs < minStart)
				minStart = e.startTimeNs;
		}

		for (uint32_t i = 0; i < latestFrame.entries.size(); ++i)
		{
			auto& entry = latestFrame.entries[i];
			if (entry.endTimeNs == 0) continue;

			float x0 = canvasPos.x + leftMargin +
				static_cast<float>((entry.startTimeNs - minStart) * nsToPixel);
			float x1 = canvasPos.x + leftMargin +
				static_cast<float>((entry.endTimeNs - minStart) * nsToPixel);
			float y0 = canvasPos.y + topMargin + entry.threadId * rowHeight + 3.0f;
			float y1 = y0 + rowHeight - 6.0f;

			// Minimum visible width
			if (x1 - x0 < 2.0f) x1 = x0 + 2.0f;

			ImU32 color = getTaskColor(i);
			drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color, 2.0f);

			// Task name label (if bar is wide enough)
			float barWidth = x1 - x0;
			if (barWidth > 30.0f)
			{
				drawList->AddText(ImVec2(x0 + 3.0f, y0 + 2.0f),
					IM_COL32(255, 255, 255, 255), entry.name.c_str());
			}

			// Hover tooltip
			ImVec2 mousePos = ImGui::GetMousePos();
			if (mousePos.x >= x0 && mousePos.x <= x1 && mousePos.y >= y0 && mousePos.y <= y1)
			{
				ImGui::BeginTooltip();
				double durationUs = static_cast<double>(entry.endTimeNs - entry.startTimeNs) / 1000.0;
				ImGui::Text("%s", entry.name.c_str());
				ImGui::Text("Duration: %.1f us", durationUs);
				ImGui::Text("Thread: %u", entry.threadId);
				ImGui::EndTooltip();
			}
		}

		// Reserve space
		ImGui::Dummy(ImVec2(canvasSize.x, topMargin + threadCount * rowHeight + 10.0f));
	}

	void TaskGraphWindow::drawStatistics()
	{
		if (stats.empty())
		{
			ImGui::TextDisabled("No statistics available");
			return;
		}

		float frameDurationMs = static_cast<float>(latestFrame.frameDurationNs) / 1e6f;
		ImGui::Text("Frame: %.3f ms", frameDurationMs);
		ImGui::Separator();

		if (ImGui::BeginTable("TaskStats", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Task");
			ImGui::TableSetupColumn("Avg (us)");
			ImGui::TableSetupColumn("Min (us)");
			ImGui::TableSetupColumn("Max (us)");
			ImGui::TableSetupColumn("% Frame");
			ImGui::TableHeadersRow();

			for (auto& s : stats)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", s.name.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", s.avgDurationUs);
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", s.minDurationUs);
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", s.maxDurationUs);
				ImGui::TableNextColumn();
				float pct = (frameDurationMs > 0.0f)
					? static_cast<float>(s.avgDurationUs / 1000.0 / frameDurationMs * 100.0)
					: 0.0f;
				ImGui::Text("%.1f%%", pct);
			}

			ImGui::EndTable();
		}
	}

	void TaskGraphWindow::drawDAG()
	{
		if (taskNames.empty())
		{
			ImGui::TextDisabled("No graph structure available");
			return;
		}

		ImGui::Text("Tasks: %zu", taskNames.size());
		ImGui::Separator();

		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImGui::GetContentRegionAvail();
		canvasSize.y = std::max(canvasSize.y, 200.0f);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(canvasPos,
			ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
			IM_COL32(30, 30, 30, 255));

		// Simple topological layout: compute layers using longest path from roots
		uint32_t nodeCount = static_cast<uint32_t>(taskNames.size());
		std::vector<uint32_t> layer(nodeCount, 0);

		// Build reverse adjacency (incoming edges)
		std::vector<std::vector<uint32_t>> inEdges(nodeCount);
		for (uint32_t i = 0; i < nodeCount; ++i)
		{
			for (uint32_t dep : adjacency[i])
			{
				inEdges[dep].push_back(i);
			}
		}

		// Compute layers (longest path from any root)
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (uint32_t i = 0; i < nodeCount; ++i)
			{
				for (uint32_t pred : inEdges[i])
				{
					if (layer[i] < layer[pred] + 1)
					{
						layer[i] = layer[pred] + 1;
						changed = true;
					}
				}
			}
		}

		uint32_t maxLayer = *std::max_element(layer.begin(), layer.end());

		// Count nodes per layer for vertical positioning
		std::vector<std::vector<uint32_t>> layerNodes(maxLayer + 1);
		for (uint32_t i = 0; i < nodeCount; ++i)
		{
			layerNodes[layer[i]].push_back(i);
		}

		// Compute node positions
		float nodeWidth = 110.0f;
		float nodeHeight = 28.0f;
		float layerSpacing = (canvasSize.x - 20.0f) / static_cast<float>(maxLayer + 1);

		struct NodePos { float x, y; };
		std::vector<NodePos> nodePositions(nodeCount);

		for (uint32_t l = 0; l <= maxLayer; ++l)
		{
			auto& nodes = layerNodes[l];
			float totalHeight = static_cast<float>(nodes.size()) * (nodeHeight + 10.0f);
			float startY = (canvasSize.y - totalHeight) * 0.5f;

			for (uint32_t idx = 0; idx < nodes.size(); ++idx)
			{
				uint32_t nodeId = nodes[idx];
				nodePositions[nodeId].x = 10.0f + l * layerSpacing;
				nodePositions[nodeId].y = startY + idx * (nodeHeight + 10.0f);
			}
		}

		// Draw edges
		for (uint32_t i = 0; i < nodeCount; ++i)
		{
			for (uint32_t dep : adjacency[i])
			{
				ImVec2 from(canvasPos.x + nodePositions[i].x + nodeWidth,
					canvasPos.y + nodePositions[i].y + nodeHeight * 0.5f);
				ImVec2 to(canvasPos.x + nodePositions[dep].x,
					canvasPos.y + nodePositions[dep].y + nodeHeight * 0.5f);

				drawList->AddLine(from, to, IM_COL32(120, 120, 120, 200), 1.5f);

				// Arrow head
				ImVec2 dir(to.x - from.x, to.y - from.y);
				float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
				if (len > 0)
				{
					dir.x /= len;
					dir.y /= len;
					ImVec2 arrowBase(to.x - dir.x * 8.0f, to.y - dir.y * 8.0f);
					ImVec2 perp(-dir.y * 4.0f, dir.x * 4.0f);
					drawList->AddTriangleFilled(
						to,
						ImVec2(arrowBase.x + perp.x, arrowBase.y + perp.y),
						ImVec2(arrowBase.x - perp.x, arrowBase.y - perp.y),
						IM_COL32(120, 120, 120, 200));
				}
			}
		}

		// Draw nodes
		for (uint32_t i = 0; i < nodeCount; ++i)
		{
			float x = canvasPos.x + nodePositions[i].x;
			float y = canvasPos.y + nodePositions[i].y;

			ImU32 color = getTaskColor(i);
			drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + nodeWidth, y + nodeHeight), color, 4.0f);
			drawList->AddRect(ImVec2(x, y), ImVec2(x + nodeWidth, y + nodeHeight),
				IM_COL32(200, 200, 200, 180), 4.0f);

			// Center text
			ImVec2 textSize = ImGui::CalcTextSize(taskNames[i].c_str());
			float textX = x + (nodeWidth - textSize.x) * 0.5f;
			float textY = y + (nodeHeight - textSize.y) * 0.5f;
			drawList->AddText(ImVec2(textX, textY), IM_COL32(255, 255, 255, 255), taskNames[i].c_str());
		}

		ImGui::Dummy(canvasSize);
	}

	void TaskGraphWindow::drawDrawCalls()
	{
		ImGui::Text("Draw Calls: %u", drawCallTotal);
		ImGui::TextDisabled("CPU-recorded draws last frame (excludes editor ImGui / gizmos / previews)");
		ImGui::Separator();

		if (drawCallTotal == 0)
		{
			ImGui::TextDisabled("No draw calls recorded");
			return;
		}

		// Category indices sorted by count, descending.
		std::array<size_t, render::FrameDrawStats::kCount> order{};
		for (size_t i = 0; i < render::FrameDrawStats::kCount; ++i) order[i] = i;
		std::sort(order.begin(), order.end(),
			[this](size_t a, size_t b) { return drawCallsByCategory[a] > drawCallsByCategory[b]; });

		const float barMaxWidth = 220.0f;
		const float barHeight = ImGui::GetTextLineHeight();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		if (ImGui::BeginTable("DrawCallBreakdown", 3,
			ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (size_t idx : order)
			{
				uint32_t count = drawCallsByCategory[idx];
				if (count == 0) continue;

				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%s", render::drawCategoryName(static_cast<render::DrawCategory>(idx)));

				ImGui::TableNextColumn();
				ImGui::Text("%u", count);

				ImGui::TableNextColumn();
				float frac = static_cast<float>(count) / static_cast<float>(drawCallTotal);
				ImVec2 p0 = ImGui::GetCursorScreenPos();
				float w = barMaxWidth * frac;
				ImU32 color = getTaskColor(static_cast<uint32_t>(idx));
				drawList->AddRectFilled(p0, ImVec2(p0.x + std::max(w, 2.0f), p0.y + barHeight), color, 2.0f);
				char overlay[32];
				snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.0f);
				drawList->AddText(ImVec2(p0.x + 4.0f, p0.y), IM_COL32(255, 255, 255, 255), overlay);
				ImGui::Dummy(ImVec2(barMaxWidth, barHeight));
			}

			ImGui::EndTable();
		}
	}

	std::string TaskGraphWindow::loadDisplayName(const asset::AssetGUID& guid,
		const std::string& debugName)
	{
		// Non-asset loads (shaders) carry their path as a debug name
		if (!debugName.empty())
		{
			return std::filesystem::path(debugName).filename().string();
		}

		auto it = loadNameCache.find(guid);
		if (it != loadNameCache.end()) return it->second;

		std::string name = guid.toString();
		try
		{
			events::assetdb::GetAssetPathQuery pathQuery;
			pathQuery.guid = guid;
			if (auto pathOpt = events::EventDispatcher::instance().query(pathQuery))
			{
				name = std::filesystem::path(*pathOpt).filename().string();
			}
		}
		catch (const std::exception&)
		{
		}
		return loadNameCache.emplace(guid, std::move(name)).first->second;
	}

	void TaskGraphWindow::drawLoading()
	{
		ImGui::Text("Pending: %u  |  In flight: %u", loadStats.pendingCount, loadStats.inFlightCount);

		// IO throughput estimate from recent completed loads that reported a size
		double totalBytes = 0.0;
		double totalLoadMs = 0.0;
		for (const auto& rec : recentLoads)
		{
			if (rec.finalStage == resource::LoadStage::Completed && rec.bytes > 0 && rec.loadMs > 0.0f)
			{
				totalBytes += static_cast<double>(rec.bytes);
				totalLoadMs += rec.loadMs;
			}
		}
		if (totalLoadMs > 0.0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("|  ~%.1f MB/s (decoded size over recent loads)",
				totalBytes / 1024.0 / 1024.0 / (totalLoadMs / 1000.0));
		}
		ImGui::Separator();

		// Active loads with progress bars
		if (activeLoads.empty())
		{
			ImGui::TextDisabled("No loads in progress");
		}
		else if (ImGui::BeginTable("ActiveLoads", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 220.0f);
			ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Wait (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (const auto& load : activeLoads)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", loadDisplayName(load.guid, load.debugName).c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%s", resource::loadStageName(load.stage));
				ImGui::TableNextColumn();
				ImGui::Text("%.2f", load.computedPriority);
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", load.queueWaitMs);
				ImGui::TableNextColumn();
				if (load.stage == resource::LoadStage::Pending)
				{
					ImGui::TextDisabled("queued");
				}
				else
				{
					// Loaders report fraction at their own granularity; running
					// without a reported fraction still shows elapsed time
					char overlay[48];
					if (load.fraction > 0.0f)
						snprintf(overlay, sizeof(overlay), "%.0f%% (%.0f ms)", load.fraction * 100.0f, load.runMs);
					else
						snprintf(overlay, sizeof(overlay), "%.0f ms", load.runMs);
					ImGui::ProgressBar(load.fraction, ImVec2(-1.0f, 0.0f), overlay);
				}
			}

			ImGui::EndTable();
		}

		ImGui::Separator();
		ImGui::Text("Recent loads");

		if (recentLoads.empty())
		{
			ImGui::TextDisabled("No completed loads yet");
			return;
		}

		if (ImGui::BeginTable("RecentLoads", 5,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
			ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 220.0f);
			ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Queue (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Load (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			// Newest first
			for (auto it = recentLoads.rbegin(); it != recentLoads.rend(); ++it)
			{
				const auto& rec = *it;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", loadDisplayName(rec.guid, rec.debugName).c_str());
				ImGui::TableNextColumn();
				switch (rec.finalStage)
				{
				case resource::LoadStage::Failed:
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Failed");
					break;
				case resource::LoadStage::Cancelled:
					ImGui::TextDisabled("Cancelled");
					break;
				default:
					ImGui::Text("OK");
					break;
				}
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", rec.queueWaitMs);
				ImGui::TableNextColumn();
				ImGui::Text("%.1f", rec.loadMs);
				ImGui::TableNextColumn();
				if (rec.bytes > 0)
					ImGui::Text("%.2f MB", static_cast<double>(rec.bytes) / 1024.0 / 1024.0);
				else
					ImGui::TextDisabled("-");
			}

			ImGui::EndTable();
		}
	}

	void TaskGraphWindow::drawGpuPasses()
	{
		auto& gpuSink = render::GpuPassStats::instance();

		if (gpuSink.isUnsupported())
		{
			ImGui::TextDisabled("GPU timestamp queries are not supported on this device");
			return;
		}

		bool enabled = gpuProfilingEnabled;
		if (ImGui::Checkbox("GPU Profiling", &enabled))
		{
			gpuProfilingEnabled = enabled;
			gpuSink.requestEnabled(enabled);
			if (!enabled)
			{
				gpuStats = {};
				gpuHistoryMs.clear();
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Per-pass render graph timings (timestamp queries, off by default)");

		if (!gpuProfilingEnabled)
		{
			ImGui::TextDisabled("Enable GPU profiling to collect pass timings");
			return;
		}

		if (!gpuStats.valid)
		{
			ImGui::TextDisabled("Waiting for first GPU readback...");
			return;
		}

		ImGui::Text("GPU frame: %.3f ms (EMA %.3f ms)  |  Barriers: %u (%u flushes)",
			gpuStats.totalMs, gpuStats.emaTotalMs, gpuStats.barrierCount, gpuStats.barrierFlushCount);

		drawFrameHistoryPlot("##gpuhistory", gpuHistoryMs);
		ImGui::Separator();

		// Passes sorted by EMA cost, descending
		std::vector<size_t> order(gpuStats.passTimings.size());
		for (size_t i = 0; i < order.size(); ++i) order[i] = i;
		std::sort(order.begin(), order.end(),
			[this](size_t a, size_t b)
			{
				return gpuStats.passTimings[a].emaMs > gpuStats.passTimings[b].emaMs;
			});

		const float barMaxWidth = 220.0f;
		const float barHeight = ImGui::GetTextLineHeight();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		if (ImGui::BeginTable("GpuPassTimings", 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 170.0f);
			ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("EMA (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (size_t idx : order)
			{
				const auto& pass = gpuStats.passTimings[idx];

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", pass.name.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass.ms);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", pass.emaMs);
				ImGui::TableNextColumn();
				float frac = (gpuStats.emaTotalMs > 0.0f) ? pass.emaMs / gpuStats.emaTotalMs : 0.0f;
				ImVec2 p0 = ImGui::GetCursorScreenPos();
				float w = barMaxWidth * frac;
				ImU32 color = getTaskColor(static_cast<uint32_t>(idx));
				drawList->AddRectFilled(p0, ImVec2(p0.x + std::max(w, 2.0f), p0.y + barHeight), color, 2.0f);
				char overlay[32];
				snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.0f);
				drawList->AddText(ImVec2(p0.x + 4.0f, p0.y), IM_COL32(255, 255, 255, 255), overlay);
				ImGui::Dummy(ImVec2(barMaxWidth, barHeight));
			}

			ImGui::EndTable();
		}
	}
}
