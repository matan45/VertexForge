#pragma once

#include "../../graphics/animation/AnimationExport.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace animator::ik
{
    struct LookAtConfig
    {
        float maxAngle = glm::radians(80.0f);     // Max rotation from forward direction
        float deadZoneAngle = glm::radians(5.0f);  // Angle within which no IK is applied
        float blendSpeed = 5.0f;                    // Smoothing speed (per second)
    };

    class VF_ANIMATION_API LookAtIKHelper
    {
    public:
        // Calculate a clamped look-at target position for a head/spine IK chain.
        // Returns the world position the chain should aim toward, clamped by maxAngle.
        // outWeight is set to the IK blend weight (0 in dead zone, 1 at full angle).
        static glm::vec3 calculateLookAtTarget(
            const glm::vec3& headWorldPosition,
            const glm::vec3& headForwardDirection,
            const glm::vec3& targetWorldPosition,
            const LookAtConfig& config = {},
            float* outWeight = nullptr);

        // Smooth a look-at target over time to prevent snapping.
        static glm::vec3 smoothTarget(
            const glm::vec3& currentTarget,
            const glm::vec3& desiredTarget,
            float deltaTime,
            float blendSpeed = 5.0f);
    };
}
