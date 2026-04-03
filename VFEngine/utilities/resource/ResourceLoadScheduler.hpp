#pragma once
#include "ResourceLoadTypes.hpp"
#include "CancellationToken.hpp"
#include "../asset/AssetGUID.hpp"
#include <mutex>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <functional>
#include <future>
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
		std::function<void()> executeLoad;

		bool operator<(const LoadRequest& other) const
		{
			return computedPriority < other.computedPriority; // max-heap: higher priority on top
		}
	};

	struct InFlightLoad
	{
		uint64_t requestId = 0;
		asset::AssetGUID guid;
		CancellationToken::Ptr cancellation;
		std::future<void> future;
	};

	class ResourceLoadScheduler
	{
	public:
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

		// Compute priority from hint and camera position
		static float computePriority(const LoadHint& hint, const glm::vec3& cameraPos);

	private:
		ResourceLoadScheduler() = default;
		~ResourceLoadScheduler() = default;
		ResourceLoadScheduler(const ResourceLoadScheduler&) = delete;
		ResourceLoadScheduler& operator=(const ResourceLoadScheduler&) = delete;

		void dispatchPending();
		void pollCompletions();

		ResourceSchedulerConfig config;
		mutable std::mutex mutex;

		// Pending requests not yet dispatched (keyed by requestId for re-prioritization)
		std::unordered_map<uint64_t, LoadRequest> pendingRequests;

		// In-flight loads currently executing in the thread pool
		std::vector<InFlightLoad> inFlightLoads;

		std::atomic<uint64_t> nextRequestId{ 1 };
		glm::vec3 lastCameraPosition{ 0.0f };

		// Per-frame stats
		uint32_t completedThisFrame = 0;
		uint32_t cancelledThisFrame = 0;
		std::atomic<bool> initialized{ false };
	};

}
