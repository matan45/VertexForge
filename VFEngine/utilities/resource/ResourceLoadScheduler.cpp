#include "ResourceLoadScheduler.hpp"
#include "../threading/JobSystem.hpp"
#include "../print/Log.hpp"
#include <algorithm>
#include <cmath>

namespace resource {

	// Importance weights: Critical=5, High=3, Normal=1, Low=0.5, Background=0.2
	static constexpr float importanceWeights[] = { 5.0f, 3.0f, 1.0f, 0.5f, 0.2f };

	ResourceLoadScheduler& ResourceLoadScheduler::instance()
	{
		static ResourceLoadScheduler inst;
		return inst;
	}

	void ResourceLoadScheduler::init(const ResourceSchedulerConfig& cfg)
	{
		std::scoped_lock lock(mutex);
		config = cfg;
		initialized.store(true, std::memory_order_release);
		vfLogInfo("ResourceLoadScheduler initialized (maxConcurrent={}, maxQueue={})",
			config.maxConcurrentLoads, config.maxQueueSize);
	}

	void ResourceLoadScheduler::shutdown()
	{
		initialized.store(false, std::memory_order_release);

		std::unique_lock lock(mutex);

		// Cancel all pending — must run their lambdas so captured promises are set
		// and pendingAsyncOps is decremented (the decrement lives inside the lambda)
		for (auto& [id, req] : pendingRequests)
		{
			if (req.cancellation)
				req.cancellation->cancel();
		}
		// Move out lambdas before clearing, then execute outside the map
		std::vector<std::function<void()>> pendingLambdas;
		pendingLambdas.reserve(pendingRequests.size());
		for (auto& [id, req] : pendingRequests)
		{
			if (req.executeLoad)
				pendingLambdas.push_back(std::move(req.executeLoad));
		}
		pendingRequests.clear();

		// Release lock while running pending lambdas (they acquire cacheMutex internally)
		lock.unlock();
		for (auto& fn : pendingLambdas)
			fn();
		lock.lock();

		// Cancel in-flight and wait
		for (auto& load : inFlightLoads)
		{
			if (load.cancellation)
				load.cancellation->cancel();
		}

		// Wait for in-flight to complete (they check cancellation internally)
		for (auto& load : inFlightLoads)
		{
			if (load.future.valid())
			{
				load.future.wait_for(std::chrono::seconds(5));
			}
		}
		inFlightLoads.clear();

		vfLogInfo("ResourceLoadScheduler shut down");
	}

	uint64_t ResourceLoadScheduler::submit(LoadRequest request)
	{
		uint64_t id = nextRequestId.fetch_add(1, std::memory_order_relaxed);
		request.requestId = id;

		if (!request.cancellation)
			request.cancellation = CancellationToken::create();

		std::scoped_lock lock(mutex);

		if (pendingRequests.size() >= config.maxQueueSize)
		{
			// Drop lowest-priority pending request to make room
			uint64_t lowestId = 0;
			float lowestPriority = std::numeric_limits<float>::max();
			for (const auto& [pid, preq] : pendingRequests)
			{
				if (preq.computedPriority < lowestPriority)
				{
					lowestPriority = preq.computedPriority;
					lowestId = pid;
				}
			}

			if (lowestId != 0 && request.computedPriority > lowestPriority)
			{
				auto it = pendingRequests.find(lowestId);
				if (it != pendingRequests.end())
				{
					if (it->second.cancellation)
						it->second.cancellation->cancel();
					pendingRequests.erase(it);
				}
			}
			else
			{
				// New request is lower priority than everything in queue; cancel it
				if (request.cancellation)
					request.cancellation->cancel();
				return id;
			}
		}

		request.computedPriority = computePriority(request.hint, lastCameraPosition);
		pendingRequests.emplace(id, std::move(request));

		// Eagerly dispatch if there's room, so loads work even without update() being called
		dispatchPending();

		return id;
	}

	void ResourceLoadScheduler::update(const glm::vec3& cameraPosition)
	{
		if (!initialized.load(std::memory_order_acquire))
			return;

		std::scoped_lock lock(mutex);
		lastCameraPosition = cameraPosition;
		completedThisFrame = 0;
		cancelledThisFrame = 0;

		// 1. Poll completions of in-flight loads
		pollCompletions();

		// 2. Recompute priorities for all pending requests based on new camera position
		for (auto& [id, req] : pendingRequests)
		{
			req.computedPriority = computePriority(req.hint, cameraPosition);
		}

		// 3. Remove cancelled pending requests
		for (auto it = pendingRequests.begin(); it != pendingRequests.end(); )
		{
			if (it->second.cancellation && it->second.cancellation->isCancelled())
			{
				it = pendingRequests.erase(it);
				++cancelledThisFrame;
			}
			else
			{
				++it;
			}
		}

		// 4. Dispatch highest-priority pending loads up to concurrency limit
		dispatchPending();
	}

	bool ResourceLoadScheduler::cancel(const asset::AssetGUID& guid)
	{
		std::scoped_lock lock(mutex);

		// Cancel pending
		for (auto it = pendingRequests.begin(); it != pendingRequests.end(); ++it)
		{
			if (it->second.guid == guid)
			{
				if (it->second.cancellation)
					it->second.cancellation->cancel();
				pendingRequests.erase(it);
				return true;
			}
		}

		// Cancel in-flight (soft cancel - let it finish but discard result)
		for (auto& load : inFlightLoads)
		{
			if (load.guid == guid)
			{
				if (load.cancellation)
					load.cancellation->cancel();
				return true;
			}
		}

		return false;
	}

	void ResourceLoadScheduler::cancelAll()
	{
		std::scoped_lock lock(mutex);

		for (auto& [id, req] : pendingRequests)
		{
			if (req.cancellation)
				req.cancellation->cancel();
		}
		pendingRequests.clear();

		for (auto& load : inFlightLoads)
		{
			if (load.cancellation)
				load.cancellation->cancel();
		}
	}

	SchedulerStats ResourceLoadScheduler::getStats() const
	{
		std::scoped_lock lock(mutex);
		return {
			static_cast<uint32_t>(pendingRequests.size()),
			static_cast<uint32_t>(inFlightLoads.size()),
			completedThisFrame,
			cancelledThisFrame
		};
	}

	float ResourceLoadScheduler::computePriority(const LoadHint& hint, const glm::vec3& cameraPos)
	{
		float distancePriority = 1.0f;
		if (hint.worldPosition.has_value())
		{
			float distance = glm::length(hint.worldPosition.value() - cameraPos);
			distancePriority = 1.0f / (1.0f + distance * 0.01f);
		}

		uint8_t idx = static_cast<uint8_t>(hint.importance);
		float importanceMultiplier = (idx < 5) ? importanceWeights[idx] : 1.0f;

		return distancePriority * importanceMultiplier + hint.priority;
	}

	void ResourceLoadScheduler::dispatchPending()
	{
		// Always poll completions first to free up concurrency slots
		pollCompletions();

		if (pendingRequests.empty())
			return;

		// Build priority queue from pending requests
		std::priority_queue<LoadRequest> queue;
		for (const auto& [id, req] : pendingRequests)
		{
			queue.push(req);
		}

		auto& jobSystem = threading::JobSystem::instance();

		while (!queue.empty() && inFlightLoads.size() < config.maxConcurrentLoads)
		{
			auto request = std::move(const_cast<LoadRequest&>(queue.top()));
			queue.pop();

			uint64_t reqId = request.requestId;
			auto it = pendingRequests.find(reqId);
			if (it == pendingRequests.end())
				continue;

			// Skip if already cancelled
			if (request.cancellation && request.cancellation->isCancelled())
			{
				pendingRequests.erase(it);
				++cancelledThisFrame;
				continue;
			}

			// Move the execute function out before erasing
			auto executeLoad = std::move(it->second.executeLoad);
			auto cancellation = it->second.cancellation;
			auto guid = it->second.guid;
			pendingRequests.erase(it);

			// Submit to JobSystem with LOW priority to avoid starving frame-critical work
			auto future = jobSystem.submit(
				[exec = std::move(executeLoad), cancel = cancellation]() {
					if (cancel && cancel->isCancelled())
						return;
					exec();
				},
				threading::JobPriority::LOW
			);

			inFlightLoads.push_back({
				reqId,
				guid,
				cancellation,
				std::move(future)
			});
		}
	}

	void ResourceLoadScheduler::pollCompletions()
	{
		for (auto it = inFlightLoads.begin(); it != inFlightLoads.end(); )
		{
			if (!it->future.valid() ||
				it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				// Retrieve the future to clear any exceptions
				if (it->future.valid())
				{
					try { it->future.get(); }
					catch (const std::exception& e) {
						vfLogError("ResourceLoadScheduler: load failed: {}", e.what());
					}
				}

				if (it->cancellation && it->cancellation->isCancelled())
					++cancelledThisFrame;
				else
					++completedThisFrame;

				it = inFlightLoads.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

}
