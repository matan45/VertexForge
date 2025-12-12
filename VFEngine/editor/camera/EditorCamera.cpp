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
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0)); // Yaw
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0)); // Pitch
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1)); // Roll
        
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
        return glm::vec3(-std::sin(yawRad), 0.0f, -std::cos(yawRad));
    }

    glm::vec3 EditorCamera::getRightDirection() const {
        float yawRad = glm::radians(rotation.y);
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
        
        rotation.y = std::fmod(rotation.y + 360.0f, 360.0f);
        
        rotation.x = std::clamp(rotation.x, -89.0f, 89.0f);

        updateViewMatrix();
    }

}
