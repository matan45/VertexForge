#pragma once

#include "../components/CoreComponents.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace scene
{
    // VK-1660: the world matrix of `entity`, composed from the LOCAL TransformComponent matrices up the
    // ParentComponent chain (world = parentWorld * local, as SceneGraphSystem computes it). Unlike
    // WorldTransformComponent it does not depend on this frame's Transforms task having run, so bake
    // code (navmesh / volumetric geometry collection) sees edits made earlier the same frame. Missing
    // transforms count as identity; the walk stops after kMaxDepth parents (corrupt cycles).
    inline glm::mat4 composeWorldMatrix(const entt::registry& registry, entt::entity entity)
    {
        constexpr int kMaxDepth = 256;
        glm::mat4 world(1.0f);
        entt::entity current = entity;
        for (int depth = 0; depth < kMaxDepth && current != entt::null && registry.valid(current); ++depth)
        {
            if (const auto* transform = registry.try_get<components::TransformComponent>(current))
            {
                world = transform->getMatrix() * world;
            }
            const auto* parent = registry.try_get<components::ParentComponent>(current);
            current = parent ? parent->parent : entt::null;
        }
        return world;
    }

    // World-space position of `entity` (the translation of composeWorldMatrix).
    inline glm::vec3 composeWorldPosition(const entt::registry& registry, entt::entity entity)
    {
        return glm::vec3(composeWorldMatrix(registry, entity)[3]);
    }
}
