#pragma once
#include "TaskGraph.hpp"

#include <vector>
#include <string>
#include <cstdint>
#include <mutex>

namespace threading {

	struct TaskProfileStats {
		std::string name;
		double avgDurationUs = 0.0;
		double minDurationUs = 0.0;
		double maxDurationUs = 0.0;
	};

	struct FrameProfileSnapshot {
		std::vector<TaskProfileEntry> entries;
		uint64_t frameDurationNs = 0;
	};

	// Editor-only task names excluded from the viewport-equivalent frame total.
	// Runtime's task graph contains none of these.
	inline constexpr const char* kEditorOnlyTaskNames[] = { "ImGuiDraw" };

	// Duration of a single named task in the snapshot (0 if absent / incomplete).
	VF_THREADING_API uint64_t taskDurationNs(const FrameProfileSnapshot& snapshot, const char* taskName);

	// frameDurationNs minus the summed durations of all editor-only tasks.
	VF_THREADING_API uint64_t viewportFrameDurationNs(const FrameProfileSnapshot& snapshot);

#pragma warning(push)
#pragma warning(disable: 4251) // std::mutex/std::vector members crossing the DLL boundary
	class VF_THREADING_API TaskProfiler {
	public:
		static constexpr size_t MAX_HISTORY = 120;

		static TaskProfiler& instance();

		void setEnabled(bool enabled);
		bool isEnabled() const;

		// Record one frame's profile data
		void recordFrame(const std::vector<TaskProfileEntry>& entries);

		// Get the most recent frame snapshot
		FrameProfileSnapshot getLatestFrame() const;

		// Get the full history ring buffer (physical ring order)
		std::vector<FrameProfileSnapshot> getHistory() const;

		// Get the full history ordered oldest -> newest (for timeline frame-scrubbing).
		std::vector<FrameProfileSnapshot> getHistoryChronological() const;

		// Compute per-task statistics across the history
		std::vector<TaskProfileStats> computeStats() const;

		// Append entries to the most recent frame snapshot (for render thread injection)
		void appendToLatestFrame(const std::vector<TaskProfileEntry>& entries);

		// Get the thread count seen in profiling data
		uint32_t getMaxThreadId() const;

	private:
		TaskProfiler() = default;

		mutable std::mutex mutex;
		std::vector<FrameProfileSnapshot> history;
		size_t writeIndex = 0;
		bool wrapped = false;
		uint32_t maxThreadId = 0;
		bool enabled = false;
	};
#pragma warning(pop)

}
