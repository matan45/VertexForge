#pragma once

#include "../../graphics/animation/AnimationExport.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>

namespace animator::ik
{
    #pragma warning(push)
    #pragma warning(disable: 4251)
    struct VF_ANIMATION_API FootIKConfig
    {
        float footHeight = 0.05f;       // Height of foot sole above ground contact
        float maxStepHeight = 0.5f;     // Max terrain deviation before IK disengages
    };

    struct VF_ANIMATION_API FootRaycastResult
    {
        bool hit = false;
        glm::vec3 hitPoint{0.0f};
        glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
        float hitDistance = 0.0f;
    };

    struct VF_ANIMATION_API FootIKResult
    {
        glm::vec3 targetPosition{0.0f};
        std::optional<glm::quat> targetRotation;
        float weight = 0.0f;
        bool isGrounded = false;
    };

    #pragma warning(pop)
    class VF_ANIMATION_API FootIKHelper
    {
    public:
        // Calculate IK target for a single foot given its current world position
        // and a raycast result from casting downward from above the foot.
        static FootIKResult calculateFootTarget(
            const glm::vec3& footWorldPosition,
            const glm::vec3& characterUp,
            const FootRaycastResult& raycastResult,
            const FootIKConfig& config = {});

        // Calculate pelvis Y-offset to prevent leg over-extension.
        // leftOriginalY/rightOriginalY are the animated foot Y positions before IK.
        static float calculatePelvisOffset(
            const FootIKResult& leftFoot,
            float leftOriginalY,
            const FootIKResult& rightFoot,
            float rightOriginalY,
            float currentOffset,
            float deltaTime,
            float adjustSpeed = 5.0f);
    };
}
