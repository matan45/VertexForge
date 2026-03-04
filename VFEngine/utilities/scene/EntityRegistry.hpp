#pragma once
#include <entt/entt.hpp>
#include <atomic>

namespace scene {
	class EntityRegistry {
	private:
		inline static entt::registry registry;
		inline static std::atomic<bool> sceneTransitioning{ false };


	public:
		inline static entt::registry& getRegistry() {
			return registry;
		}

		static void setSceneTransitioning(bool value) {
			sceneTransitioning.store(value, std::memory_order_release);
		}

		static bool isSceneTransitioning() {
			return sceneTransitioning.load(std::memory_order_acquire);
		}
	};
}
