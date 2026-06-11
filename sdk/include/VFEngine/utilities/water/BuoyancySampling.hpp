#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include "../types/PhysicsTypes.hpp"

namespace water
{
    inline constexpr uint32_t MAX_BUOYANCY_POINTS = 8;

    struct BuoyancySamplePoints
    {
        glm::vec3 points[MAX_BUOYANCY_POINTS]{};
        uint32_t count = 0;
    };

    // Local-space buoyancy sample points for a collider shape. Points are biased toward
    // the hull bottom so differential submersion produces a natural righting torque.
    // size follows ColliderComponent conventions: Box = half extents, Sphere/Capsule = radius in x.
    inline BuoyancySamplePoints generateSamplePoints(types::ColliderShape shape,
                                                     const glm::vec3& size,
                                                     float height,
                                                     const glm::vec3& offset)
    {
        BuoyancySamplePoints result;
        auto add = [&result](const glm::vec3& p)
        {
            if (result.count < MAX_BUOYANCY_POINTS)
                result.points[result.count++] = p;
        };

        switch (shape)
        {
        case types::ColliderShape::Box:
            add(offset);
            add(offset + glm::vec3(-size.x, -size.y, -size.z));
            add(offset + glm::vec3(size.x, -size.y, -size.z));
            add(offset + glm::vec3(-size.x, -size.y, size.z));
            add(offset + glm::vec3(size.x, -size.y, size.z));
            break;
        case types::ColliderShape::Sphere:
            add(offset);
            add(offset + glm::vec3(-size.x, 0.0f, 0.0f));
            add(offset + glm::vec3(size.x, 0.0f, 0.0f));
            add(offset + glm::vec3(0.0f, 0.0f, -size.x));
            add(offset + glm::vec3(0.0f, 0.0f, size.x));
            break;
        case types::ColliderShape::Capsule:
            add(offset);
            add(offset + glm::vec3(0.0f, -height * 0.5f, 0.0f));
            add(offset + glm::vec3(0.0f, height * 0.5f, 0.0f));
            break;
        default:
            add(offset);
            break;
        }
        return result;
    }

    // Vertical half extent the buoyancy loop treats an entity as occupying, matching the
    // single-point heuristic the system always used.
    inline float colliderHalfHeight(types::ColliderShape shape, const glm::vec3& size, float height)
    {
        switch (shape)
        {
        case types::ColliderShape::Box: return size.y;
        case types::ColliderShape::Sphere: return size.x;
        case types::ColliderShape::Capsule: return size.x + height * 0.5f;
        default: return 0.5f;
        }
    }

    // Submersion ratio of one sample point treated as a vertical span of `extent` rising
    // from the point: 0 when the water surface is at or below the point, 1 once the
    // surface is `extent` above it. Bottom-biased points therefore engage first.
    inline float computeSubmersion(float pointWorldY, float waterHeight, float extent)
    {
        float span = glm::max(extent, 0.0001f);
        return glm::clamp((waterHeight - pointWorldY) / span, 0.0f, 1.0f);
    }
}
