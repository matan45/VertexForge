#pragma once
#include <cstdint>
#include <optional>
#include <glm/glm.hpp>

namespace resource {

	enum class LoadImportance : uint8_t
	{
		Critical = 0,   // UI-blocking, shaders, player avatar
		High = 1,       // Visible on screen now
		Normal = 2,     // Nearby, likely visible soon
		Low = 3,        // Prefetch
		Background = 4  // Speculative
	};

	struct LoadHint
	{
		float priority = 0.5f;
		LoadImportance importance = LoadImportance::Normal;
		std::optional<glm::vec3> worldPosition;
		uint32_t sectorId = 0;
	};

	struct ResourceSchedulerConfig
	{
		uint32_t maxConcurrentLoads = 8;
		uint32_t maxQueueSize = 256;
	};

	struct SchedulerStats
	{
		uint32_t pendingCount = 0;
		uint32_t inFlightCount = 0;
		uint32_t completedThisFrame = 0;
		uint32_t cancelledThisFrame = 0;
	};

}
