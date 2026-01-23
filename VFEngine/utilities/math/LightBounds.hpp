#pragma once
#include "Frustum.hpp"
#include <glm/glm.hpp>
#include <cmath>

namespace math
{
    // Utility struct for computing bounding volumes for different light types
    struct LightBounds
    {
        // Compute AABB for a point light (sphere bounding box)
        // position: world-space position of the light
        // radius: light radius/range
        static AABB computePointLightAABB(const glm::vec3& position, float radius)
        {
            return AABB(
                position - glm::vec3(radius),
                position + glm::vec3(radius)
            );
        }

        // Compute AABB for a spot light (cone bounding box)
        // Uses a bounding sphere approximation for the cone
        // position: world-space position (apex of cone)
        // direction: normalized direction the light points
        // range: how far the light reaches
        // outerAngle: outer cone angle in degrees
        static AABB computeSpotLightAABB(const glm::vec3& position,
                                          const glm::vec3& direction,
                                          float range,
                                          float outerAngle)
        {
            float halfAngle = glm::radians(outerAngle);
            float cosAngle = std::cos(halfAngle);
            float sinAngle = std::sin(halfAngle);

            // For a cone, we can compute a bounding sphere
            // The sphere center is along the cone axis, and radius depends on cone dimensions
            // Using the formula for minimal bounding sphere of a cone:
            // If angle < 45 degrees, sphere center is at apex
            // Otherwise, sphere center is at range/(2*cos(angle)) along axis

            if (cosAngle > 0.707f)  // angle < 45 degrees
            {
                // Tight bound: sphere at apex with radius = range
                return AABB(
                    position - glm::vec3(range),
                    position + glm::vec3(range)
                );
            }
            else
            {
                // Wider cone: use bounding sphere that encompasses the cone
                float sphereRadius = range * sinAngle / cosAngle;  // Base radius
                sphereRadius = std::max(sphereRadius, range);      // At least range

                glm::vec3 coneEnd = position + direction * range;
                glm::vec3 center = (position + coneEnd) * 0.5f;
                float boundRadius = glm::length(coneEnd - position) * 0.5f + sphereRadius * 0.5f;

                return AABB(
                    center - glm::vec3(boundRadius),
                    center + glm::vec3(boundRadius)
                );
            }
        }

        // Compute AABB for a directional light
        // WARNING: Directional lights have no spatial bounds (they affect everything).
        // Do NOT use this for BVH - directional lights should be stored separately
        // and always included in query results without spatial culling.
        // This function is kept for compatibility but should rarely be needed.
        [[deprecated("Directional lights should not be stored in BVH - handle separately")]]
        static AABB computeDirectionalLightAABB()
        {
            constexpr float inf = std::numeric_limits<float>::max() * 0.5f;
            return AABB(
                glm::vec3(-inf),
                glm::vec3(inf)
            );
        }
    };
}
