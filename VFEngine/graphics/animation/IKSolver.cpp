#include "IKSolver.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace animation
{
    FABRIKSolver::ChainResult FABRIKSolver::solve(
        const ChainInput& input,
        const animator::ik::IKTarget& target,
        const animator::ik::IKSolveParams& params)
    {
        ChainResult result;
        const size_t numJoints = input.positions.size();

        if (numJoints < 2)
        {
            result.positions = input.positions;
            result.rotations = input.rotations;
            return result;
        }

        result.positions = input.positions;
        result.rotations = input.rotations;

        // Compute total chain length to check reachability
        float totalLength = 0.0f;
        for (const auto& len : input.boneLengths)
            totalLength += len;

        const glm::vec3 rootPos = input.positions[0];
        const float distToTarget = glm::length(target.position - rootPos);

        // If target is unreachable, extend fully toward it
        if (distToTarget > totalLength)
        {
            glm::vec3 direction = glm::normalize(target.position - rootPos);
            result.positions[0] = rootPos;
            for (size_t i = 1; i < numJoints; ++i)
            {
                result.positions[i] = result.positions[i - 1] + direction * input.boneLengths[i - 1];
            }
        }
        else
        {
            // FABRIK iterative solve
            for (int iter = 0; iter < params.maxIterations; ++iter)
            {
                // Check convergence
                float tipDist = glm::length(result.positions[numJoints - 1] - target.position);
                if (tipDist < params.tolerance)
                    break;

                // Forward pass: from tip toward root
                forwardPass(result.positions, input.boneLengths, target.position);

                // Backward pass: from root toward tip
                backwardPass(result.positions, input.boneLengths, rootPos);

                // Apply constraints after each iteration
                if (!input.constraints.empty())
                {
                    applyConstraints(result.positions, result.rotations,
                                     input.boneLengths, input.constraints,
                                     input.rotations);
                }
            }
        }

        // Recompute rotations from solved positions
        for (size_t i = 0; i < numJoints - 1; ++i)
        {
            glm::vec3 originalDir = glm::normalize(input.positions[i + 1] - input.positions[i]);
            glm::vec3 solvedDir = glm::normalize(result.positions[i + 1] - result.positions[i]);

            glm::quat deltaRotation = computeRotationBetween(originalDir, solvedDir);
            result.rotations[i] = deltaRotation * input.rotations[i];
        }

        // Apply target rotation to tip if provided
        if (target.rotation.has_value() && numJoints > 0)
        {
            result.rotations[numJoints - 1] = target.rotation.value();
        }

        return result;
    }

    void FABRIKSolver::forwardPass(std::vector<glm::vec3>& positions,
                                    const std::vector<float>& boneLengths,
                                    const glm::vec3& target)
    {
        const size_t n = positions.size();
        positions[n - 1] = target;

        for (size_t i = n - 2; i < n; --i) // unsigned wrap-around handles i == 0
        {
            glm::vec3 dir = positions[i] - positions[i + 1];
            float len = glm::length(dir);
            if (len > 1e-6f)
                dir /= len;
            else
                dir = glm::vec3(0.0f, 1.0f, 0.0f);

            positions[i] = positions[i + 1] + dir * boneLengths[i];
        }
    }

    void FABRIKSolver::backwardPass(std::vector<glm::vec3>& positions,
                                     const std::vector<float>& boneLengths,
                                     const glm::vec3& rootPos)
    {
        positions[0] = rootPos;

        for (size_t i = 1; i < positions.size(); ++i)
        {
            glm::vec3 dir = positions[i] - positions[i - 1];
            float len = glm::length(dir);
            if (len > 1e-6f)
                dir /= len;
            else
                dir = glm::vec3(0.0f, 1.0f, 0.0f);

            positions[i] = positions[i - 1] + dir * boneLengths[i - 1];
        }
    }

    void FABRIKSolver::applyConstraints(std::vector<glm::vec3>& positions,
                                         std::vector<glm::quat>& rotations,
                                         const std::vector<float>& boneLengths,
                                         const std::vector<animator::ik::JointConstraint>& constraints,
                                         const std::vector<glm::quat>& originalRotations)
    {
        const size_t numJoints = positions.size();
        const size_t numConstraints = constraints.size();

        for (size_t i = 1; i < numJoints - 1; ++i)
        {
            if (i >= numConstraints)
                continue;

            const auto& constraint = constraints[i];
            if (constraint.type == animator::ik::JointConstraintType::None)
                continue;

            // Compute the current local rotation at this joint
            glm::vec3 parentDir = glm::normalize(positions[i] - positions[i - 1]);
            glm::vec3 childDir = glm::normalize(positions[i + 1] - positions[i]);

            glm::quat localRotation = computeRotationBetween(parentDir, childDir);
            glm::quat parentWorldRot = (i > 0 && i - 1 < originalRotations.size())
                                            ? originalRotations[i - 1]
                                            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

            glm::quat constrained = applyConstraint(localRotation, parentWorldRot, constraint);

            // If the constraint changed the rotation, reposition the child
            if (constrained != localRotation)
            {
                glm::vec3 constrainedDir = constrained * parentDir;
                positions[i + 1] = positions[i] + glm::normalize(constrainedDir) * boneLengths[i];
            }
        }
    }

    glm::quat FABRIKSolver::applyConstraint(
        const glm::quat& localRotation,
        const glm::quat& parentWorldRotation,
        const animator::ik::JointConstraint& constraint)
    {
        switch (constraint.type)
        {
        case animator::ik::JointConstraintType::Hinge:
            return constrainToHinge(localRotation, constraint.hingeAxis);

        case animator::ik::JointConstraintType::Cone:
            return constrainToCone(localRotation, constraint.coneAngle);

        case animator::ik::JointConstraintType::BallAndSocket:
            return constrainToBallAndSocket(localRotation,
                                            constraint.swingAngle,
                                            constraint.twistMin,
                                            constraint.twistMax);

        case animator::ik::JointConstraintType::None:
        default:
            return localRotation;
        }
    }

    glm::quat FABRIKSolver::computeRotationBetween(const glm::vec3& from, const glm::vec3& to)
    {
        float dot = glm::clamp(glm::dot(from, to), -1.0f, 1.0f);

        // Nearly parallel - no rotation needed
        if (dot > 0.9999f)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        // Nearly anti-parallel - 180 degree rotation around an arbitrary perpendicular axis
        if (dot < -0.9999f)
        {
            glm::vec3 perp = glm::abs(from.x) < 0.9f
                                 ? glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f))
                                 : glm::cross(from, glm::vec3(0.0f, 1.0f, 0.0f));
            perp = glm::normalize(perp);
            return glm::angleAxis(glm::pi<float>(), perp);
        }

        glm::vec3 axis = glm::cross(from, to);
        float axisLen = glm::length(axis);
        if (axisLen < 1e-6f)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        axis /= axisLen;
        float angle = std::acos(dot);
        return glm::angleAxis(angle, axis);
    }

    glm::quat FABRIKSolver::constrainToHinge(const glm::quat& rotation,
                                               const glm::vec3& axis)
    {
        // Project the rotation onto the hinge axis
        // Extract the component of rotation around the specified axis
        glm::vec3 normalizedAxis = glm::normalize(axis);

        // Decompose rotation into twist (around axis) and swing (away from axis)
        glm::vec3 rotAxis = glm::vec3(rotation.x, rotation.y, rotation.z);
        float dot = glm::dot(rotAxis, normalizedAxis);

        // Twist quaternion: component along the hinge axis
        glm::quat twist;
        twist.w = rotation.w;
        twist.x = normalizedAxis.x * dot;
        twist.y = normalizedAxis.y * dot;
        twist.z = normalizedAxis.z * dot;

        float len = glm::length(glm::vec4(twist.x, twist.y, twist.z, twist.w));
        if (len > 1e-6f)
            twist = twist / len;
        else
            twist = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        return twist;
    }

    glm::quat FABRIKSolver::constrainToCone(const glm::quat& rotation,
                                              float maxAngle)
    {
        // Clamp the rotation angle to maxAngle
        float angle = glm::angle(rotation);
        if (angle <= maxAngle)
            return rotation;

        glm::vec3 axis = glm::axis(rotation);
        if (glm::length(axis) < 1e-6f)
            return rotation;

        return glm::angleAxis(maxAngle, glm::normalize(axis));
    }

    glm::quat FABRIKSolver::constrainToBallAndSocket(const glm::quat& rotation,
                                                       float swingAngle,
                                                       float twistMin,
                                                       float twistMax)
    {
        // Swing-twist decomposition around the forward axis (Z)
        glm::vec3 twistAxis = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 rotAxis = glm::vec3(rotation.x, rotation.y, rotation.z);
        float dot = glm::dot(rotAxis, twistAxis);

        // Extract twist component
        glm::quat twist;
        twist.w = rotation.w;
        twist.x = twistAxis.x * dot;
        twist.y = twistAxis.y * dot;
        twist.z = twistAxis.z * dot;

        float twistLen = glm::length(glm::vec4(twist.x, twist.y, twist.z, twist.w));
        if (twistLen > 1e-6f)
            twist = twist / twistLen;
        else
            twist = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        // Extract swing component
        glm::quat swing = rotation * glm::conjugate(twist);

        // Constrain swing (cone limit)
        float swingRot = glm::angle(swing);
        if (swingRot > swingAngle)
        {
            glm::vec3 swingAxis = glm::axis(swing);
            if (glm::length(swingAxis) > 1e-6f)
                swing = glm::angleAxis(swingAngle, glm::normalize(swingAxis));
        }

        // Constrain twist
        float twistRot = glm::angle(twist);
        glm::vec3 twistRotAxis = glm::axis(twist);

        // Determine twist sign
        float twistSign = glm::dot(twistRotAxis, twistAxis) >= 0.0f ? 1.0f : -1.0f;
        float signedTwist = twistSign * twistRot;

        signedTwist = glm::clamp(signedTwist, twistMin, twistMax);

        twist = glm::angleAxis(signedTwist, twistAxis);

        return swing * twist;
    }
}
