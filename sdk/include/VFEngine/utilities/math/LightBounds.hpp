#pragma once
#include "Frustum.hpp"
#include <glm/glm.hpp>
#include <cmath>

namespace math
{
    struct LightBounds
    {
        static AABB computePointLightAABB(const glm::vec3& position, float radius)
        {
            return AABB(
                position - glm::vec3(radius),
                position + glm::vec3(radius)
            );
        }

        static AABB computeSpotLightAABB(const glm::vec3& position,
                                          const glm::vec3& direction,
                                          float range,
                                          float outerAngle)
        {
            float halfAngle = glm::radians(outerAngle);
            float cosAngle = std::cos(halfAngle);
            float sinAngle = std::sin(halfAngle);

            if (cosAngle > 0.707f)
            {
                return AABB(
                    position - glm::vec3(range),
                    position + glm::vec3(range)
                );
            }
            else
            {
                float sphereRadius = range * sinAngle / cosAngle;
                sphereRadius = std::max(sphereRadius, range);

                glm::vec3 coneEnd = position + direction * range;
                glm::vec3 center = (position + coneEnd) * 0.5f;
                float boundRadius = glm::length(coneEnd - position) * 0.5f + sphereRadius * 0.5f;

                return AABB(
                    center - glm::vec3(boundRadius),
                    center + glm::vec3(boundRadius)
                );
            }
        }
    };
}
