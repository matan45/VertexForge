#pragma once

#include "../../graphics/animation/AnimationExport.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>

namespace animator::ik
{
    #pragma warning(push)
    #pragma warning(disable: 4251)
    struct VF_ANIMATION_API HandIKConfig
    {
        float maxReachDistance = 1.5f;   // Max distance hand can reach before IK disengages
        float gripRotationBlend = 1.0f; // 0 = keep animation rotation, 1 = fully match target rotation
    };

    struct VF_ANIMATION_API HandIKResult
    {
        glm::vec3 targetPosition{0.0f};
        std::optional<glm::quat> targetRotation;
        float weight = 0.0f;
        bool isReachable = false;
    };

    #pragma warning(pop)
    class VF_ANIMATION_API HandIKHelper
    {
    public:
        // Calculate IK target for a hand/arm chain to reach a world position.
        // shoulderWorldPosition is the root of the arm chain (used to check reach distance).
        static HandIKResult calculateHandTarget(
            const glm::vec3& handWorldPosition,
            const glm::vec3& shoulderWorldPosition,
            const glm::vec3& targetWorldPosition,
            const std::optional<glm::quat>& targetRotation = std::nullopt,
            const HandIKConfig& config = {});

        // Calculate a two-handed weapon grip: given a dominant hand position and
        // an offset for the off-hand grip point, returns the off-hand IK target.
        static HandIKResult calculateTwoHandedGrip(
            const glm::vec3& dominantHandWorldPosition,
            const glm::quat& dominantHandWorldRotation,
            const glm::vec3& gripOffset,
            const glm::vec3& offHandShoulderWorldPosition,
            const HandIKConfig& config = {});
    };
}
