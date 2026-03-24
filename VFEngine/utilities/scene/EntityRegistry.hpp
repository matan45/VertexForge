#pragma once
#include <entt/entt.hpp>
#include <atomic>
#include <unordered_map>

namespace scene {
	class EntityRegistry {
	private:
		inline static entt::registry registry;
		inline static std::atomic<bool> sceneTransitioning{ false };
		inline static std::atomic<int> transitionSkipsRemaining{ 0 };

		inline static std::unordered_map<uint64_t, entt::entity> uuidToEntity;
		inline static std::unordered_map<uint32_t, uint64_t> entityToUuid;
		inline static bool initialized = false;

		// If the viewport is hidden/closed during a transition, the flag could stay
		// stuck forever. This cap ensures it auto-clears after N skip attempts.
		static constexpr int MAX_TRANSITION_SKIPS = 4;

	public:
		static void init();

		inline static entt::registry& getRegistry() {
			return registry;
		}

		static entt::entity findByUUID(uint64_t uuid) {
			auto it = uuidToEntity.find(uuid);
			return (it != uuidToEntity.end()) ? it->second : entt::null;
		}

		static void insertUUID(uint64_t uuid, entt::entity entity) {
			uuidToEntity[uuid] = entity;
			entityToUuid[static_cast<uint32_t>(entity)] = uuid;
		}

		static void removeUUID(uint64_t uuid, entt::entity entity) {
			uuidToEntity.erase(uuid);
			entityToUuid.erase(static_cast<uint32_t>(entity));
		}

		static void removeEntityMapping(entt::entity entity) {
			auto it = entityToUuid.find(static_cast<uint32_t>(entity));
			if (it != entityToUuid.end()) {
				uuidToEntity.erase(it->second);
			}
		}

		static void setSceneTransitioning(bool value) {
			sceneTransitioning.store(value, std::memory_order_release);
			if (value) {
				transitionSkipsRemaining.store(MAX_TRANSITION_SKIPS, std::memory_order_release);
			}
		}

		static bool isSceneTransitioning() {
			return sceneTransitioning.load(std::memory_order_acquire);
		}

		// Checks the flag and decrements the skip counter. If the counter expires
		// the flag is auto-cleared, preventing it from being stuck forever when
		// the viewport panel is hidden or no caller clears it.
		static bool consumeTransitionSkip() {
			if (!sceneTransitioning.load(std::memory_order_acquire)) {
				return false;
			}
			int remaining = transitionSkipsRemaining.fetch_sub(1, std::memory_order_acq_rel);
			if (remaining <= 0) {
				sceneTransitioning.store(false, std::memory_order_release);
				return false;
			}
			return true;
		}
	};
}
