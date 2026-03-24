#include "EntityRegistry.hpp"
#include "../components/CoreComponents.hpp"

namespace scene
{
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
}
