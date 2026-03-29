#include "EntityRegistry.hpp"
#include "../components/CoreComponents.hpp"

namespace scene
{
    // Static member definitions
    entt::registry EntityRegistry::registry;
    std::atomic<bool> EntityRegistry::sceneTransitioning{ false };
    std::atomic<int> EntityRegistry::transitionSkipsRemaining{ 0 };
    std::unordered_map<uint64_t, entt::entity> EntityRegistry::uuidToEntity;
    std::unordered_map<uint32_t, uint64_t> EntityRegistry::entityToUuid;
    bool EntityRegistry::initialized = false;

    namespace
    {
        void onUUIDConstruct(entt::registry& reg, entt::entity entity)
        {
            auto& comp = reg.get<components::UUIDComponent>(entity);
            uint64_t uuid = comp.id.getValue();
            EntityRegistry::insertUUID(uuid, entity);
        }

        void onUUIDUpdate(entt::registry& reg, entt::entity entity)
        {
            EntityRegistry::removeEntityMapping(entity);

            auto& comp = reg.get<components::UUIDComponent>(entity);
            uint64_t uuid = comp.id.getValue();
            EntityRegistry::insertUUID(uuid, entity);
        }

        void onUUIDDestroy(entt::registry& reg, entt::entity entity)
        {
            auto& comp = reg.get<components::UUIDComponent>(entity);
            uint64_t uuid = comp.id.getValue();
            EntityRegistry::removeUUID(uuid, entity);
        }
    }

    void EntityRegistry::init()
    {
        if (initialized)
            return;

        registry.on_construct<components::UUIDComponent>().connect<&onUUIDConstruct>();
        registry.on_update<components::UUIDComponent>().connect<&onUUIDUpdate>();
        registry.on_destroy<components::UUIDComponent>().connect<&onUUIDDestroy>();

        initialized = true;
    }

    entt::registry& EntityRegistry::getRegistry()
    {
        return registry;
    }

    entt::entity EntityRegistry::findByUUID(uint64_t uuid)
    {
        auto it = uuidToEntity.find(uuid);
        return (it != uuidToEntity.end()) ? it->second : entt::null;
    }

    void EntityRegistry::insertUUID(uint64_t uuid, entt::entity entity)
    {
        uuidToEntity[uuid] = entity;
        entityToUuid[static_cast<uint32_t>(entity)] = uuid;
    }

    void EntityRegistry::removeUUID(uint64_t uuid, entt::entity entity)
    {
        uuidToEntity.erase(uuid);
        entityToUuid.erase(static_cast<uint32_t>(entity));
    }

    void EntityRegistry::removeEntityMapping(entt::entity entity)
    {
        auto it = entityToUuid.find(static_cast<uint32_t>(entity));
        if (it != entityToUuid.end()) {
            uuidToEntity.erase(it->second);
            entityToUuid.erase(it);
        }
    }

    void EntityRegistry::setSceneTransitioning(bool value)
    {
        sceneTransitioning.store(value, std::memory_order_release);
        if (value) {
            transitionSkipsRemaining.store(MAX_TRANSITION_SKIPS, std::memory_order_release);
        }
    }

    bool EntityRegistry::isSceneTransitioning()
    {
        return sceneTransitioning.load(std::memory_order_acquire);
    }

    bool EntityRegistry::consumeTransitionSkip()
    {
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
}
