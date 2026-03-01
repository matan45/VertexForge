#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

namespace animator::ik
{
    enum class JointConstraintType : uint8_t
    {
        None,
        Hinge,
        Cone,
        BallAndSocket
    };

    struct JointConstraint
    {
        JointConstraintType type = JointConstraintType::None;

        // Hinge: single-axis rotation (e.g., elbow, knee)
        glm::vec3 hingeAxis{0.0f, 1.0f, 0.0f};

        // Cone: symmetric swing limit
        float coneAngle = glm::radians(45.0f);

        // BallAndSocket: asymmetric swing + twist limits
        float swingAngle = glm::radians(45.0f);
        float twistMin = glm::radians(-45.0f);
        float twistMax = glm::radians(45.0f);
    };

    struct IKTarget
    {
        glm::vec3 position{0.0f};
        std::optional<glm::quat> rotation;
    };

    struct IKSolveParams
    {
        int maxIterations = 10;
        float tolerance = 0.001f;
    };

    const char* constraintTypeToString(JointConstraintType type);
    JointConstraintType stringToConstraintType(const std::string& str);
}
