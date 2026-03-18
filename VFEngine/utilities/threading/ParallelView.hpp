#pragma once
#include "JobSystem.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace threading {

	constexpr uint32_t PARALLEL_VIEW_THRESHOLD = 256;

	// Pre-assure component storages on the main thread before parallel access.
	// EnTT's registry.view() internally calls assure<T>() which writes to the
	// storage map if the type hasn't been seen yet. Two parallel jobs triggering
	// assure() for different types corrupts the internal storage map.
	template<typename... Components>
	void assureStorages(entt::registry& registry)
	{
		(registry.storage<Components>(), ...);
	}

	// Parallel iteration over an EnTT view. Materializes entities into a vector,
	// then distributes ranges to worker threads via JobSystem::parallelFor.
	// Falls back to sequential iteration if entity count < PARALLEL_VIEW_THRESHOLD.
	template<typename... Components, typename Func>
	void parallelView(entt::registry& registry, Func&& func,
		uint32_t minBatchSize = 64)
	{
		auto view = registry.view<Components...>();

		if (view.size_hint() < PARALLEL_VIEW_THRESHOLD)
		{
			for (auto entity : view)
			{
				func(entity);
			}
			return;
		}

		// Materialize entities into a contiguous vector for indexed parallel access.
		// NOT thread_local: worker threads access this via captured reference.
		std::vector<entt::entity> entities;
		entities.reserve(view.size_hint());
		for (auto entity : view)
		{
			entities.push_back(entity);
		}

		uint32_t count = static_cast<uint32_t>(entities.size());
		if (count == 0) return;

		if (count < PARALLEL_VIEW_THRESHOLD)
		{
			for (auto entity : entities)
			{
				func(entity);
			}
			return;
		}

		JobSystem::instance().parallelFor(count,
			[&entities, &func](uint32_t begin, uint32_t end)
			{
				for (uint32_t i = begin; i < end; ++i)
				{
					func(entities[i]);
				}
			}, minBatchSize);
	}

}
