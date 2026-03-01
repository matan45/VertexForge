#pragma once

#include "animator/IKTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace animation
{
    class FABRIKSolver
    {
    public:
        struct ChainInput
        {
            std::vector<glm::vec3> positions;
            std::vector<glm::quat> rotations;
            std::vector<float> boneLengths;
            std::vector<animator::ik::JointConstraint> constraints;
        };

        struct ChainResult
        {
            std::vector<glm::vec3> positions;
            std::vector<glm::quat> rotations;
        };

        static ChainResult solve(
            const ChainInput& input,
            const animator::ik::IKTarget& target,
            const animator::ik::IKSolveParams& params = {}
        );

        static glm::quat applyConstraint(
            const glm::quat& localRotation,
            const glm::quat& parentWorldRotation,
            const animator::ik::JointConstraint& constraint
        );

    private:
        static void forwardPass(std::vector<glm::vec3>& positions,
                                const std::vector<float>& boneLengths,
                                const glm::vec3& target);

        static void backwardPass(std::vector<glm::vec3>& positions,
                                 const std::vector<float>& boneLengths,
                                 const glm::vec3& rootPos);

        static void applyConstraints(std::vector<glm::vec3>& positions,
                                     std::vector<glm::quat>& rotations,
                                     const std::vector<float>& boneLengths,
                                     const std::vector<animator::ik::JointConstraint>& constraints,
                                     const std::vector<glm::quat>& originalRotations);

        static glm::quat computeRotationBetween(const glm::vec3& from, const glm::vec3& to);

        static glm::quat constrainToHinge(const glm::quat& rotation,
                                          const glm::vec3& axis);

        static glm::quat constrainToCone(const glm::quat& rotation,
                                         float maxAngle);

        static glm::quat constrainToBallAndSocket(const glm::quat& rotation,
                                                  float swingAngle,
                                                  float twistMin,
                                                  float twistMax);
    };
}
