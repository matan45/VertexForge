#include "TaskProfiler.hpp"

#include <algorithm>
#include <unordered_map>
#include <limits>

namespace threading {

	TaskProfiler& TaskProfiler::instance()
	{
		static TaskProfiler inst;
		return inst;
	}

	void TaskProfiler::setEnabled(bool enabledValue)
	{
		std::lock_guard<std::mutex> lock(mutex);
		enabled = enabledValue;
		if (!enabled)
		{
			history.clear();
			writeIndex = 0;
			wrapped = false;
			maxThreadId = 0;
		}
	}

	bool TaskProfiler::isEnabled() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return enabled;
	}

	void TaskProfiler::recordFrame(const std::vector<TaskProfileEntry>& entries)
	{
		if (!isEnabled()) return;

		FrameProfileSnapshot snapshot;
		snapshot.entries = entries;

		// Compute frame duration from earliest start to latest end
		uint64_t minStart = std::numeric_limits<uint64_t>::max();
		uint64_t maxEnd = 0;
		for (auto& e : entries) {
			if (e.endTimeNs > 0) {
				minStart = std::min(minStart, e.startTimeNs);
				maxEnd = std::max(maxEnd, e.endTimeNs);
			}
		}
		snapshot.frameDurationNs = (maxEnd > minStart) ? (maxEnd - minStart) : 0;

		std::lock_guard<std::mutex> lock(mutex);

		// Track max thread ID
		for (auto& e : entries) {
			maxThreadId = std::max(maxThreadId, e.threadId);
		}

		if (history.size() < MAX_HISTORY) {
			history.push_back(std::move(snapshot));
		}
		else {
			history[writeIndex] = std::move(snapshot);
		}
		writeIndex = (writeIndex + 1) % MAX_HISTORY;
		if (history.size() >= MAX_HISTORY) wrapped = true;
	}

	FrameProfileSnapshot TaskProfiler::getLatestFrame() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (history.empty()) return {};
		size_t idx = (writeIndex == 0) ? (history.size() - 1) : (writeIndex - 1);
		return history[idx];
	}

	std::vector<FrameProfileSnapshot> TaskProfiler::getHistory() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return history;
	}

	std::vector<TaskProfileStats> TaskProfiler::computeStats() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (history.empty()) return {};

		// Accumulate per-task stats across all frames
		struct Accumulator {
			double sum = 0.0;
			double minVal = std::numeric_limits<double>::max();
			double maxVal = 0.0;
			uint32_t count = 0;
		};
		std::unordered_map<std::string, Accumulator> accum;

		for (auto& frame : history) {
			for (auto& entry : frame.entries) {
				if (entry.endTimeNs == 0) continue;
				double durationUs = static_cast<double>(entry.endTimeNs - entry.startTimeNs) / 1000.0;
				auto& a = accum[entry.name];
				a.sum += durationUs;
				a.minVal = std::min(a.minVal, durationUs);
				a.maxVal = std::max(a.maxVal, durationUs);
				a.count++;
			}
		}

		std::vector<TaskProfileStats> result;
		result.reserve(accum.size());
		for (auto& [name, a] : accum) {
			TaskProfileStats stats;
			stats.name = name;
			stats.avgDurationUs = (a.count > 0) ? (a.sum / a.count) : 0.0;
			stats.minDurationUs = (a.count > 0) ? a.minVal : 0.0;
			stats.maxDurationUs = a.maxVal;
			result.push_back(std::move(stats));
		}

		// Sort by average duration (descending)
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
			return a.avgDurationUs > b.avgDurationUs;
		});

		return result;
	}

	void TaskProfiler::appendToLatestFrame(const std::vector<TaskProfileEntry>& entries)
	{
		if (!isEnabled()) return;
		if (entries.empty()) return;

		std::lock_guard<std::mutex> lock(mutex);
		if (history.empty()) return;

		size_t idx = (writeIndex == 0) ? (history.size() - 1) : (writeIndex - 1);
		auto& snapshot = history[idx];

		for (auto& e : entries)
		{
			snapshot.entries.push_back(e);
			maxThreadId = std::max(maxThreadId, e.threadId);
		}

		// Recompute frame duration to include render thread
		uint64_t minStart = std::numeric_limits<uint64_t>::max();
		uint64_t maxEnd = 0;
		for (auto& e : snapshot.entries)
		{
			if (e.endTimeNs > 0)
			{
				minStart = std::min(minStart, e.startTimeNs);
				maxEnd = std::max(maxEnd, e.endTimeNs);
			}
		}
		snapshot.frameDurationNs = (maxEnd > minStart) ? (maxEnd - minStart) : 0;
	}

	uint32_t TaskProfiler::getMaxThreadId() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return maxThreadId;
	}

	uint64_t taskDurationNs(const FrameProfileSnapshot& snapshot, const char* taskName)
	{
		for (const auto& e : snapshot.entries)
			if (e.endTimeNs > e.startTimeNs && e.name == taskName)
				return e.endTimeNs - e.startTimeNs;
		return 0;
	}

	uint64_t viewportFrameDurationNs(const FrameProfileSnapshot& snapshot)
	{
		uint64_t editorNs = 0;
		for (const char* name : kEditorOnlyTaskNames)
			editorNs += taskDurationNs(snapshot, name);
		return (snapshot.frameDurationNs > editorNs) ? (snapshot.frameDurationNs - editorNs) : 0;
	}

}
