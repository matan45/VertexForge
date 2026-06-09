#include "TaskGraphWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/threading/TaskGraphEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "imgui.h"
#include "print/Log.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace windows
{
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
			}
			else
			{
				latestFrame = {};
				stats.clear();
				maxThreadId = 0;
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
}
