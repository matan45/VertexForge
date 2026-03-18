#pragma once
#include "JobSystem.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace threading {

	// Parallel collect: iterate view, filter + transform, build output vector.
	// Each thread builds a local vector, then merge. Zero contention on hot path.
	template<typename T, typename... Components, typename Filter, typename Transform>
	std::vector<T> parallelCollect(entt::registry& registry,
		Filter&& filter, Transform&& transform,
		uint32_t minBatchSize = 64)
	{
		auto view = registry.view<Components...>();

		// Materialize entities for indexed access
		thread_local std::vector<entt::entity> entities;
		entities.clear();
		entities.reserve(view.size_hint());
		for (auto entity : view)
		{
			entities.push_back(entity);
		}

		uint32_t count = static_cast<uint32_t>(entities.size());
		if (count == 0) return {};

		// Sequential fallback for small counts
		constexpr uint32_t PARALLEL_COLLECT_THRESHOLD = 256;
		if (count < PARALLEL_COLLECT_THRESHOLD)
		{
			std::vector<T> result;
			result.reserve(count);
			for (auto entity : entities)
			{
				if (filter(entity))
				{
					result.push_back(transform(entity));
				}
			}
			return result;
		}

		// Per-thread local vectors indexed by range partition
		uint32_t threadCount = JobSystem::instance().getThreadCount() + 1; // +1 for calling thread
		std::vector<std::vector<T>> threadResults(threadCount);

		JobSystem::instance().parallelFor(count,
			[&](uint32_t begin, uint32_t end)
			{
				// Use a simple thread-local index based on the range start
				// Each parallelFor partition gets a unique range, so we hash begin to pick a slot
				uint32_t slot = begin % threadCount;
				auto& localResults = threadResults[slot];

				for (uint32_t i = begin; i < end; ++i)
				{
					auto entity = entities[i];
					if (filter(entity))
					{
						localResults.push_back(transform(entity));
					}
				}
			}, minBatchSize);

		// Merge results
		size_t totalSize = 0;
		for (const auto& v : threadResults)
		{
			totalSize += v.size();
		}

		std::vector<T> result;
		result.reserve(totalSize);
		for (auto& v : threadResults)
		{
			result.insert(result.end(),
				std::make_move_iterator(v.begin()),
				std::make_move_iterator(v.end()));
		}

		return result;
	}

}
