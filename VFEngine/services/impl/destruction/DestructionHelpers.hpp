#pragma once
#include "../../data/EntityHandle.hpp"
#include <scene/EntityRegistry.hpp>
#include <entt/entt.hpp>

namespace services::destruction_internal
{
    inline entt::entity fromHandle(EntityHandle handle)
    {
        return static_cast<entt::entity>(static_cast<uint32_t>(handle.id));
    }

    inline bool isValidHandle(EntityHandle handle, entt::registry& registry)
    {
        if (!handle.isValid()) return false;
        return registry.valid(fromHandle(handle));
    }

    inline EntityHandle toHandle(entt::entity entity)
    {
        EntityHandle handle;
        handle.id = static_cast<uint64_t>(static_cast<uint32_t>(entity));
        return handle;
    }
}
