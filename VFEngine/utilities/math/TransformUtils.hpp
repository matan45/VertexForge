#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace math
{
    struct DecomposedTransform
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};  // Euler angles in degrees (XYZ order)
        glm::vec3 scale{1.0f};
    };

    // Decompose a transformation matrix into position, rotation (Euler degrees), and scale
    // Rotation uses XYZ order to match TransformComponent::getMatrix()
    inline DecomposedTransform decomposeMatrix(const glm::mat4& matrix)
    {
        DecomposedTransform result;
        glm::quat rotationQuat;
        glm::vec3 skew;
        glm::vec4 perspective;

        glm::decompose(matrix, result.scale, rotationQuat, result.position, skew, perspective);

        // Extract Euler angles in XYZ order (matching compose order)
        // Build rotation matrix from quaternion and extract XYZ angles
        glm::mat4 rotationMatrix = glm::mat4_cast(rotationQuat);
        float x, y, z;
        glm::extractEulerAngleXYZ(rotationMatrix, x, y, z);
        result.rotation = glm::degrees(glm::vec3(x, y, z));

        return result;
    }

    // Compose a transformation matrix from position, rotation (Euler degrees), and scale
    inline glm::mat4 composeMatrix(const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale)
    {
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, scale);
        return transform;
    }
}
