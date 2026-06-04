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

    struct IKChainConfig
    {
        std::string chainName;
        std::string tipBoneName;
        std::vector<std::string> chainBoneNames; // root-to-tip order
        std::vector<JointConstraint> constraints; // per-bone constraints
        float weight = 1.0f;
        bool enabled = true;
    };

    inline const char* constraintTypeToString(JointConstraintType type)
    {
        switch (type)
        {
        case JointConstraintType::None:           return "None";
        case JointConstraintType::Hinge:          return "Hinge";
        case JointConstraintType::Cone:           return "Cone";
        case JointConstraintType::BallAndSocket:  return "BallAndSocket";
        default:                                  return "None";
        }
    }

    inline JointConstraintType stringToConstraintType(const std::string& str)
    {
        if (str == "Hinge"          || str == "hinge")          return JointConstraintType::Hinge;
        if (str == "Cone"           || str == "cone")           return JointConstraintType::Cone;
        if (str == "BallAndSocket"  || str == "ballAndSocket")  return JointConstraintType::BallAndSocket;
        if (str == "None"           || str == "none")           return JointConstraintType::None;
        return JointConstraintType::None;
    }
}
