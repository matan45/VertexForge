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
	// VK-1592: the load tables' Type column. AssetType::COUNT means the load carries no
	// type at all (shaders, and anything submitted before VK-1434 tagging) - render a
	// dash rather than assetTypeName's "Unknown", which reads like a lookup failure.
	static void drawLoadTypeCell(resource::AssetType type)
	{
		if (type == resource::AssetType::COUNT)
			ImGui::TextDisabled("-");
		else
			ImGui::TextUnformatted(resource::assetTypeName(type));
	}

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
		// Per-pass GPU timestamps cost real GPU time, so only collect them while
		// someone is looking. This has to run BEFORE the !visible early-out: ImGui's
		// close button writes `visible` behind our back, so the close edge is only
		// ever observable on a frame where we would otherwise have returned already.
		const bool wantGpuCapture = visible && gpuProfilingEnabled;
		if (wantGpuCapture != gpuCaptureActive)
		{
			render::GpuPassStats::instance().requestEnabled(wantGpuCapture);
			gpuCaptureActive = wantGpuCapture;
			if (!wantGpuCapture)
			{
				gpuStats = {};
			}
			// Re-arm so reopening refreshes immediately rather than showing up to
			// REFRESH_INTERVAL of stale rows from the last time it was open.
			refreshTimer = REFRESH_INTERVAL;
		}

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
		// Title is load-bearing: imgui.ini keys saved layouts by it ([Window][Task
		// Graph Profiler]), so renaming orphans every existing user's docked position.
		// Discoverability of the GPU profiler is handled by the Debug menu entry instead.
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
				if (paused)
				{
					// Freeze the current history ring so the Timeline tab can scrub it.
					frozenHistory = threading::TaskProfiler::instance().getHistoryChronological();
					scrubIndex = static_cast<int>(frozenHistory.size()) - 1;
				}
				else
				{
					frozenHistory.clear();
					scrubIndex = -1;
				}
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
				const ImGuiTabItemFlags timelineTabFlags =
					selectTimelineTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				selectTimelineTab = false;
				if (ImGui::BeginTabItem("Timeline", nullptr, timelineTabFlags))
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
				const ImGuiTabItemFlags gpuTabFlags =
					selectGpuTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				selectGpuTab = false;
				if (ImGui::BeginTabItem("GPU Passes", nullptr, gpuTabFlags))
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

			// GPU timings flow through the GpuPassStats sink, independent of CPU
			// task-graph profiling. Deliberately does NOT read the enable request
			// back: gpuProfilingEnabled is this window's own checkbox, and draw()
			// is the only writer of the request. Reading it back would let any
			// other requester flip the checkbox under the user.
			auto& gpuSink = render::GpuPassStats::instance();
			// Whole-frame span history is tier 1 — always collected, so the plot is
			// already populated when the window opens.
			gpuHistoryMs = gpuSink.frameMsHistory();
			gpuStats = gpuCaptureActive ? gpuSink.snapshot() : render::GpuFrameStats{};
		}
		catch (const std::exception&)
		{
			// Query handlers not yet registered
		}
	}

	void TaskGraphWindow::drawTimeline()
	{
		// When paused, scrub the frozen history ring; otherwise track the latest frame.
		const bool scrubbing = paused && !frozenHistory.empty();
		if (scrubbing)
		{
			scrubIndex = std::clamp(scrubIndex, 0, static_cast<int>(frozenHistory.size()) - 1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderInt("##framescrub", &scrubIndex, 0,
				static_cast<int>(frozenHistory.size()) - 1, "Frame %d (oldest 0 -> newest)");
		}
		const threading::FrameProfileSnapshot& frame =
			scrubbing ? frozenHistory[static_cast<size_t>(scrubIndex)] : latestFrame;

		if (frame.entries.empty())
		{
			ImGui::TextDisabled("No profiling data available");
			return;
		}

		// Lane count from the displayed frame's own entries (a scrubbed historical
		// frame may have used fewer worker threads than the global max).
		uint32_t frameMaxThreadId = 0;
		for (const auto& e : frame.entries)
			frameMaxThreadId = std::max(frameMaxThreadId, e.threadId);

		float frameDurationMs = static_cast<float>(frame.frameDurationNs) / 1e6f;
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
		uint32_t threadCount = frameMaxThreadId + 1;
		float timelineWidth = canvasSize.x - leftMargin - 10.0f;

		if (frame.frameDurationNs == 0) return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Background
		drawList->AddRectFilled(canvasPos,
			ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + topMargin + threadCount * rowHeight + 10.0f),
			IM_COL32(30, 30, 30, 255));

		// Thread labels
		for (uint32_t t = 0; t <= frameMaxThreadId; ++t)
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
		double nsToPixel = static_cast<double>(timelineWidth) / static_cast<double>(frame.frameDurationNs);

		// Find earliest start for offset
		uint64_t minStart = UINT64_MAX;
		for (auto& e : frame.entries)
		{
			if (e.endTimeNs > 0 && e.startTimeNs < minStart)
				minStart = e.startTimeNs;
		}

		for (uint32_t i = 0; i < frame.entries.size(); ++i)
		{
			auto& entry = frame.entries[i];
			if (entry.endTimeNs == 0) continue;

			float x0 = canvasPos.x + leftMargin +
				static_cast<float>((entry.startTimeNs - minStart) * nsToPixel);
			float x1 = canvasPos.x + leftMargin +
				static_cast<float>((entry.endTimeNs - minStart) * nsToPixel);
			float y0 = canvasPos.y + topMargin + entry.threadId * rowHeight + 3.0f;
			float y1 = y0 + rowHeight - 6.0f;

			// Minimum visible width
			if (x1 - x0 < 2.0f) x1 = x0 + 2.0f;

			// Color by task NAME (not entry index) so a task keeps its color across
			// frames even as entry order / worker assignment shifts.
			ImU32 color = getTaskColor(static_cast<uint32_t>(std::hash<std::string>{}(entry.name)));
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
				ImGui::Text("This frame: %.1f us", durationUs);
				ImGui::Text("Thread: %u", entry.threadId);
				// Aggregate stats across the history ring (computeStats), matched by name.
				for (const auto& s : stats)
				{
					if (s.name == entry.name)
					{
						ImGui::Separator();
						ImGui::Text("avg %.1f us | min %.1f us | max %.1f us",
							s.avgDurationUs, s.minDurationUs, s.maxDurationUs);
						break;
					}
				}
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
		else if (ImGui::BeginTable("ActiveLoads", 6,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 220.0f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
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
				drawLoadTypeCell(load.assetType);
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

		if (ImGui::BeginTable("RecentLoads", 6,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
			ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthFixed, 220.0f);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
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
				drawLoadTypeCell(rec.assetType);
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

	void TaskGraphWindow::drawGpuPassTable(const char* id, const std::vector<size_t>& rows, float denomMs)
	{
		const float barMaxWidth = 220.0f;
		const float barHeight = ImGui::GetTextLineHeight();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		if (!ImGui::BeginTable(id, 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
		{
			return;
		}

		ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 170.0f);
		ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("EMA (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (size_t idx : rows)
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

			const float rawFrac = (denomMs > 0.0f) ? (pass.emaMs / denomMs) : 0.0f;
			// Report the real share but clamp the geometry: a sub-scope overlapping
			// other work can read past 100%, and an unclamped bar overruns its column.
			const float barFrac = std::clamp(rawFrac, 0.0f, 1.0f);
			ImVec2 p0 = ImGui::GetCursorScreenPos();
			float w = barMaxWidth * barFrac;
			ImU32 color = getTaskColor(static_cast<uint32_t>(idx));
			drawList->AddRectFilled(p0, ImVec2(p0.x + std::max(w, 2.0f), p0.y + barHeight), color, 2.0f);
			char overlay[32];
			snprintf(overlay, sizeof(overlay), "%.0f%%", rawFrac * 100.0f);
			drawList->AddText(ImVec2(p0.x + 4.0f, p0.y), IM_COL32(255, 255, 255, 255), overlay);
			ImGui::Dummy(ImVec2(barMaxWidth, barHeight));
		}

		ImGui::EndTable();
	}

	void TaskGraphWindow::drawGpuPasses()
	{
		auto& gpuSink = render::GpuPassStats::instance();

		if (gpuSink.isUnsupported())
		{
			ImGui::TextDisabled("GPU timestamp queries are not supported on this device");
			return;
		}

		// Tier 1: the whole-frame span. Two timestamps a frame, always collected,
		// so this reads even with per-pass profiling off.
		if (gpuSink.hasFrameGpuTime())
		{
			ImGui::Text("GPU frame: %.3f ms (EMA %.3f ms)",
				gpuSink.frameGpuMs(), gpuSink.emaFrameGpuMs());
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Whole-frame GPU span for the offscreen command buffer.\n"
					"Graphics queue only: async-compute work is submitted separately\n"
					"and carries no timestamps.");
			}
		}
		else
		{
			ImGui::TextDisabled("Waiting for the first GPU frame readback...");
		}

		drawFrameHistoryPlot("##gpuhistory", gpuHistoryMs);
		ImGui::Separator();

		// VK-1610: before the per-pass toggle, because this costs no timestamps and stays
		// readable with Tier 2 off.
		drawTerrainRVTSection();

		// Tier 2: one timestamp per render-graph pass boundary. Not free, so it is
		// off by default and draw() drops the request when the window closes.
		bool enabled = gpuProfilingEnabled;
		if (ImGui::Checkbox("Per-pass GPU profiling", &enabled))
		{
			// draw() owns the request — it ANDs this with `visible` and edge-detects.
			gpuProfilingEnabled = enabled;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Timestamps every render-graph pass boundary (off by default)");

		if (!gpuProfilingEnabled)
		{
			ImGui::TextDisabled("Enable per-pass profiling to break the frame down by pass");
			return;
		}

		if (!gpuStats.valid)
		{
			ImGui::TextDisabled("Waiting for first GPU readback...");
			return;
		}

		ImGui::Text("Graph passes: %.3f ms (EMA %.3f ms)  |  Barriers: %u (%u flushes)",
			gpuStats.totalMs, gpuStats.emaTotalMs, gpuStats.barrierCount, gpuStats.barrierFlushCount);
		if (gpuStats.droppedSamples > 0)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "|  dropped: %u", gpuStats.droppedSamples);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Frames whose timestamps were not ready in time and so\n"
					"contributed no sample. Expected to stay at 0 in steady state.");
			}
		}

		// Split the graph passes from the aux sub-scopes. Aux scopes are recorded
		// INSIDE a graph pass, so their cost is already in the total above — listing
		// them as siblings is what used to push the shares past 100%.
		std::vector<size_t> graphRows;
		std::vector<size_t> auxRows;
		for (size_t i = 0; i < gpuStats.passTimings.size(); ++i)
		{
			(gpuStats.passTimings[i].isAux ? auxRows : graphRows).push_back(i);
		}

		auto byEmaDesc = [this](size_t a, size_t b)
		{
			return gpuStats.passTimings[a].emaMs > gpuStats.passTimings[b].emaMs;
		};
		std::sort(graphRows.begin(), graphRows.end(), byEmaDesc);
		std::sort(auxRows.begin(), auxRows.end(), byEmaDesc);

		// Denominator is the sum of the graph rows' EMAs, NOT emaTotalMs. The
		// boundary chain makes the RAW parts sum to the raw total, but the EMAs seed
		// independently — a pass appearing mid-session (SSR/Clouds/Upscale toggling)
		// seeds exact from its first sample while emaTotalMs is still converging, so
		// dividing by emaTotalMs lets the shares transiently read past 100%. Summing
		// the rows keeps the column structurally at 100%.
		float emaDenom = 0.0f;
		for (size_t i : graphRows) emaDenom += gpuStats.passTimings[i].emaMs;

		drawGpuPassTable("GpuPassTimings", graphRows, emaDenom);

		if (!auxRows.empty())
		{
			ImGui::Separator();
			if (ImGui::TreeNodeEx("Sub-scopes", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::TextDisabled("Recorded inside SceneMeshes, so already counted above —");
				ImGui::TextDisabled("and each overlaps other GPU work, so read them as upper bounds.");
				// Same denominator: these are fractions of the same frame, just nested.
				drawGpuPassTable("GpuAuxTimings", auxRows, emaDenom);
				ImGui::TreePop();
			}
		}
	}

	void TaskGraphWindow::drawTerrainRVTSection()
	{
		const render::TerrainRVTFrameStats rvt = render::TerrainRVTStats::instance().snapshot();
		if (!rvt.active)
			return;

		if (!ImGui::TreeNodeEx("Terrain RVT residency", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		const float used = rvt.capacityPages > 0
			? static_cast<float>(rvt.residentPages) / static_cast<float>(rvt.capacityPages)
			: 0.0f;
		ImGui::Text("Pool: %ux%u, %u planes @ %u B/texel, %u MB budget",
			rvt.poolDim, rvt.poolDim, rvt.planeCount, rvt.bytesPerTexel, rvt.budgetMB);
		ImGui::Text("Pages: %u / %u resident (%.0f%%)", rvt.residentPages, rvt.capacityPages, used * 100.0f);
		ImGui::Text("This frame: %u requested, %u allocated, %u evicted, %u baked",
			rvt.requestedPages, rvt.allocatedPages, rvt.evictedPages, rvt.scheduledBakes);
		if (rvt.uncoveredSkipped > 0)
		{
			ImGui::TextDisabled("  %u requested pages lie outside loaded terrain (skipped on purpose)",
				rvt.uncoveredSkipped);
		}
		ImGui::TextDisabled("Residency CPU: %llu us readback + %llu us decode + %llu us plan",
			static_cast<unsigned long long>(rvt.readbackUs),
			static_cast<unsigned long long>(rvt.decodeUs),
			static_cast<unsigned long long>(rvt.residencyUs));

		// The whole point of the section: separate "streaming in" from "the pool is too small".
		// Both verdicts come from the residency plan's unclamped miss count — deriving them from
		// the number of pages actually allocated cannot work, because a pool with no room produces
		// a SHORTER allocation list, which would read as a quiet, healthy frame.
		if (rvt.poolLimited)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
				"THRASHING: %u pages wanted but the pool has no room", rvt.unmetPages);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("The atlas cannot hold what the camera can see, so pages evict and\n"
					"re-bake every frame — which shows up as GPU time in the VT/RVT Bake\n"
					"scope and nowhere else. Raise the RVT pool budget under Render Config >\n"
					"Virtual Texturing. Detail maps take the pool from 2 planes to 4, so the\n"
					"same MB buys roughly 40%% of the pages.");
			}
		}
		else if (rvt.budgetLimited)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
				"Streaming: %u pages queued behind the per-frame budget", rvt.unmetPages);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Benign and self-correcting — the pool has room, it is just pacing\n"
					"how many pages it bakes per frame. Only raise Pages / Frame if this\n"
					"never clears while the camera is still.");
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Residency steady");
		}

		ImGui::TreePop();
	}
}
