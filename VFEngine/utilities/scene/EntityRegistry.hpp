#pragma once
#include "ECSRegistryExport.hpp"
#include <entt/entt.hpp>
#include <atomic>
#include <unordered_map>

namespace scene {

	#pragma warning(push)
	#pragma warning(disable: 4251)
	class VF_ECSREGISTRY_API EntityRegistry {
	private:
		static entt::registry& registryRef;
		static std::atomic<bool> sceneTransitioning;
		static std::atomic<int> transitionSkipsRemaining;

		static std::unordered_map<uint64_t, entt::entity> uuidToEntity;
		static std::unordered_map<uint32_t, uint64_t> entityToUuid;
		static bool initialized;

		// If the viewport is hidden/closed during a transition, the flag could stay
		// stuck forever. This cap ensures it auto-clears after N skip attempts.
		static constexpr int MAX_TRANSITION_SKIPS = 4;

	public:
		static void init();

		static entt::registry& getRegistry();

		static entt::entity findByUUID(uint64_t uuid);

		static void insertUUID(uint64_t uuid, entt::entity entity);

		static void removeUUID(uint64_t uuid, entt::entity entity);

		static void removeEntityMapping(entt::entity entity);

		static void setSceneTransitioning(bool value);

		static bool isSceneTransitioning();

		// Checks the flag and decrements the skip counter. If the counter expires
		// the flag is auto-cleared, preventing it from being stuck forever when
		// the viewport panel is hidden or no caller clears it.
		static bool consumeTransitionSkip();
	};
	#pragma warning(pop)
}
