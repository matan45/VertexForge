#pragma once

#include "../data/EntityConversion.hpp"
#include "../data/EntityHandle.hpp"
#include "scene/Entity.hpp"

#include <vector>

namespace services::internal
{
    inline void collectSubtreeHandles(const scene::Entity& entity, std::vector<EntityHandle>& out)
    {
        out.push_back(toHandle(entity.getHandle()));
        for (const auto& child : entity.getChildren())
            collectSubtreeHandles(child, out);
    }

    inline std::vector<EntityHandle> collectSubtreeHandles(const scene::Entity& root)
    {
        std::vector<EntityHandle> handles;
        collectSubtreeHandles(root, handles);
        return handles;
    }
}
