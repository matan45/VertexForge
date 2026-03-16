#pragma once
#include <entt/entt.hpp>
#include <atomic>

namespace scene {
	class EntityRegistry {
	private:
		inline static entt::registry registry;
		inline static std::atomic<bool> sceneTransitioning{ false };
		inline static std::atomic<int> transitionSkipsRemaining{ 0 };

		// If the viewport is hidden/closed during a transition, the flag could stay
		// stuck forever. This cap ensures it auto-clears after N skip attempts.
		static constexpr int MAX_TRANSITION_SKIPS = 4;

	public:
		inline static entt::registry& getRegistry() {
			return registry;
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
