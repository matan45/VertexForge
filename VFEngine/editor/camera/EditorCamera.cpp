#include "EditorCamera.hpp"
#include <cmath>
#include <algorithm>

namespace editor {

    EditorCamera::EditorCamera() {
        updateProjectionMatrix();
        updateViewMatrix();
    }

    void EditorCamera::updateProjectionMatrix() {
        projectionMatrix = glm::perspective(
            glm::radians(fieldOfView),
            aspectRatio,
            nearPlane,
            farPlane
        );
        // Flip Y for Vulkan coordinate system (GLM is designed for OpenGL)
        projectionMatrix[1][1] *= -1;
    }

    void EditorCamera::updateViewMatrix() {
        // For FPS-style camera: apply yaw (Y) first, then pitch (X)
        // This ensures pitch always rotates around the camera's local X axis
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0)); // Yaw
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0)); // Pitch
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1)); // Roll

        // View matrix is the inverse of the camera's model matrix
        viewMatrix = glm::inverse(transform);
    }

    void EditorCamera::setAspectRatio(float aspect) {
        if (aspectRatio != aspect) {
            aspectRatio = aspect;
            updateProjectionMatrix();
        }
    }

    glm::vec3 EditorCamera::getForwardDirection() const {
        float yawRad = glm::radians(rotation.y);
        // Forward is -Z in camera space, rotated by yaw
        // When yaw=0, forward should be (0, 0, -1)
        // When yaw=90 (turned right), forward should be (-1, 0, 0)
        return glm::vec3(-std::sin(yawRad), 0.0f, -std::cos(yawRad));
    }

    glm::vec3 EditorCamera::getRightDirection() const {
        float yawRad = glm::radians(rotation.y);
        // Right is +X in camera space, rotated by yaw
        // When yaw=0, right should be (1, 0, 0)
        // When yaw=90 (turned right), right should be (0, 0, -1)
        return glm::vec3(std::cos(yawRad), 0.0f, -std::sin(yawRad));
    }

    void EditorCamera::processKeyboardInput(float deltaTime, bool forward, bool backward,
                                             bool left, bool right, bool up, bool down, bool sprint) {
        float actualSpeed = moveSpeed * deltaTime;
        if (sprint) {
            actualSpeed *= 2.0f;
        }

        glm::vec3 forwardDir = getForwardDirection();
        glm::vec3 rightDir = getRightDirection();

        if (forward) {
            position += forwardDir * actualSpeed;
        }
        if (backward) {
            position -= forwardDir * actualSpeed;
        }
        if (left) {
            position -= rightDir * actualSpeed;
        }
        if (right) {
            position += rightDir * actualSpeed;
        }
        if (up) {
            position.y += actualSpeed;
        }
        if (down) {
            position.y -= actualSpeed;
        }

        updateViewMatrix();
    }

    void EditorCamera::processMouseMovement(float xOffset, float yOffset) {
        rotation.y -= xOffset * mouseSensitivity;
        rotation.x -= yOffset * mouseSensitivity;

        // Normalize yaw to [0, 360) range using proper modulo
        rotation.y = std::fmod(rotation.y + 360.0f, 360.0f);

        // Clamp pitch to avoid gimbal lock
        rotation.x = std::clamp(rotation.x, -89.0f, 89.0f);

        updateViewMatrix();
    }

}
