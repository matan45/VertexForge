#pragma once
#include "ResourceLoadTypes.hpp"
#include "CancellationToken.hpp"
#include "../asset/AssetGUID.hpp"
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <unordered_map>
#include <functional>
#include <future>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace resource {

	struct LoadRequest
	{
		uint64_t requestId = 0;
		asset::AssetGUID guid;
		float computedPriority = 0.0f;
		LoadHint hint;
		CancellationToken::Ptr cancellation;
		LoadProgress::Ptr progress;
		std::function<void()> executeLoad;
		// Display label for loads without a database entry (e.g. shaders);
		// asset loads resolve their name from the GUID in the UI
		std::string debugName;
		std::chrono::steady_clock::time_point submitTime;

		bool operator<(const LoadRequest& other) const
		{
			return computedPriority < other.computedPriority; // max-heap: higher priority on top
		}
	};

	struct InFlightLoad
	{
		uint64_t requestId = 0;
		asset::AssetGUID guid;
		float computedPriority = 0.0f;
		CancellationToken::Ptr cancellation;
		LoadProgress::Ptr progress;
		std::string debugName;
		std::chrono::steady_clock::time_point submitTime;
		std::chrono::steady_clock::time_point dispatchTime;
		std::future<void> future;
	};

	// Snapshot of a pending or in-flight load for the profiler UI
	struct ActiveLoadInfo
	{
		uint64_t requestId = 0;
		asset::AssetGUID guid;
		std::string debugName;
		float computedPriority = 0.0f;
		LoadStage stage = LoadStage::Pending;
		float fraction = 0.0f;
		float queueWaitMs = 0.0f; // pending: time queued so far; in-flight: final queue wait
		float runMs = 0.0f;       // in-flight: time executing so far
	};

	// Finished load (completed/failed/cancelled) kept in a small ring for
	// the profiler UI's recent-loads table and IO throughput estimate
	struct CompletedLoadRecord
	{
		uint64_t requestId = 0;
		asset::AssetGUID guid;
		std::string debugName;
		LoadStage finalStage = LoadStage::Completed;
		float queueWaitMs = 0.0f;
		float loadMs = 0.0f;
		uint64_t bytes = 0;
	};

	class ResourceLoadScheduler
	{
	public:
		static constexpr size_t kCompletionHistory = 256;

		static ResourceLoadScheduler& instance();

		void init(const ResourceSchedulerConfig& cfg = {});
		void shutdown();

		// Submit a load request; returns the assigned request ID
		uint64_t submit(LoadRequest request);

		// Called each frame to re-prioritize and dispatch pending loads
		void update(const glm::vec3& cameraPosition);

		// Cancel a pending or in-flight load by GUID
		bool cancel(const asset::AssetGUID& guid);

		// Cancel all pending loads
		void cancelAll();

		SchedulerStats getStats() const;

		// Profiler UI snapshots
		std::vector<ActiveLoadInfo> getActiveLoads() const;
		std::vector<CompletedLoadRecord> getRecentCompletions() const;

		// Progress handle of a pending or in-flight load (null when idle)
		LoadProgress::Ptr findProgress(const asset::AssetGUID& guid) const;

		// Compute priority from hint and camera position
		static float computePriority(const LoadHint& hint, const glm::vec3& cameraPos);

	private:
		ResourceLoadScheduler() = default;
		~ResourceLoadScheduler() = default;
		ResourceLoadScheduler(const ResourceLoadScheduler&) = delete;
		ResourceLoadScheduler& operator=(const ResourceLoadScheduler&) = delete;

		void dispatchPending();
		void pollCompletions();
		void recordCompletion(CompletedLoadRecord record);

		ResourceSchedulerConfig config;
		mutable std::mutex mutex;

		// Pending requests not yet dispatched (keyed by requestId for re-prioritization)
		std::unordered_map<uint64_t, LoadRequest> pendingRequests;

		// In-flight loads currently executing in the thread pool
		std::vector<InFlightLoad> inFlightLoads;

		// Recent terminal loads, ring buffer
		std::vector<CompletedLoadRecord> completionHistory;
		size_t completionNext = 0;

		std::atomic<uint64_t> nextRequestId{ 1 };
		glm::vec3 lastCameraPosition{ 0.0f };

		// Per-frame stats
		uint32_t completedThisFrame = 0;
		uint32_t cancelledThisFrame = 0;
		std::atomic<bool> initialized{ false };
	};

}
