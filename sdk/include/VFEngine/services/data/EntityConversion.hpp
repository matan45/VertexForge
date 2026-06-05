#pragma once
#include "EntityHandle.hpp"
#include <entt/entt.hpp>

namespace services::internal {

    // Ensure entt::entity is 32-bit - if EnTT version changes, this will catch it at compile time
    static_assert(sizeof(entt::entity) == sizeof(uint32_t),
        "entt::entity must be 32-bit for EntityHandle conversion to work correctly");

    inline EntityHandle toHandle(entt::entity entity) {
        return EntityHandle{ static_cast<uint64_t>(static_cast<uint32_t>(entity)) };
    }

    inline entt::entity fromHandle(EntityHandle handle) {
        return static_cast<entt::entity>(static_cast<uint32_t>(handle.id));
    }

    inline bool isValidHandle(EntityHandle handle, entt::registry& registry) {
        if (!handle.isValid()) return false;
        auto entity = fromHandle(handle);
        return registry.valid(entity);
    }

}
