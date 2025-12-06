#include "InputServiceImpl.hpp"
#include "../../core/controllers/InputController.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace services {

    InputServiceImpl::InputServiceImpl(controllers::InputController* inputController)
        : inputController(inputController) {}

    bool InputServiceImpl::isKeyDown(int keyCode) const {
        if (!inputController) return false;
        return inputController->isKeyDown(keyCode);
    }

    bool InputServiceImpl::isKeyPressed(int keyCode) const {
        // For now, same as isKeyDown - would need state tracking for true press detection
        return isKeyDown(keyCode);
    }

    bool InputServiceImpl::isKeyReleased(int keyCode) const {
        if (!inputController) return false;
        return inputController->isKeyReleased(keyCode);
    }

    bool InputServiceImpl::isMouseButtonDown(int button) const {
        if (!inputController) return false;
        return inputController->isMouseButtonDown(button);
    }

    bool InputServiceImpl::isMouseButtonPressed(int button) const {
        // For now, same as isMouseButtonDown
        return isMouseButtonDown(button);
    }

    bool InputServiceImpl::isMouseButtonReleased(int button) const {
        if (!inputController) return false;
        return inputController->isMouseButtonReleased(button);
    }

    glm::vec2 InputServiceImpl::getMousePosition() const {
        if (!inputController) return glm::vec2(0.0f);
        double xpos, ypos;
        inputController->getCursorPos(xpos, ypos);
        return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    }

    glm::vec2 InputServiceImpl::getMouseDelta() const {
        // Mouse delta tracking requires frame-to-frame state.
        // ViewPort handles this directly via ImGui for camera control.
        // If needed, this could be implemented with GLFW callbacks.
        return glm::vec2(0.0f);
    }

    glm::vec2 InputServiceImpl::getScrollDelta() const {
        // Scroll tracking requires GLFW scroll callback.
        // Not currently implemented - return zero.
        return glm::vec2(0.0f);
    }

    CameraMovement InputServiceImpl::calculateCameraMovement(
        float deltaTime,
        float moveSpeed,
        float mouseSensitivity,
        bool useMouseLook) const {

        CameraMovement movement;

        // Only process if right mouse button is held (camera look mode)
        if (!isMouseButtonDown(MouseButtons::Right)) {
            return movement;
        }

        // Calculate rotation from mouse delta
        // Note: Mouse delta tracking is handled by ViewPort directly via ImGui.
        // This method currently returns zero rotation delta.
        // For full implementation, use getMouseDelta() when delta tracking is added.
        if (useMouseLook) {
            glm::vec2 delta = getMouseDelta();
            movement.deltaRotation.x = delta.x * mouseSensitivity;  // yaw
            movement.deltaRotation.y = delta.y * mouseSensitivity;  // pitch
        }

        // Calculate movement direction based on WASD
        glm::vec3 direction{ 0.0f };

        if (isKeyDown(Keys::W)) {
            direction.z -= 1.0f;  // Forward
        }
        if (isKeyDown(Keys::S)) {
            direction.z += 1.0f;  // Backward
        }
        if (isKeyDown(Keys::A)) {
            direction.x -= 1.0f;  // Left
        }
        if (isKeyDown(Keys::D)) {
            direction.x += 1.0f;  // Right
        }
        if (isKeyDown(Keys::E)) {
            direction.y += 1.0f;  // Up
        }
        if (isKeyDown(Keys::Q)) {
            direction.y -= 1.0f;  // Down
        }

        // Normalize if moving diagonally
        if (glm::length(direction) > 0.0f) {
            direction = glm::normalize(direction);
        }

        // Apply speed and delta time
        float actualSpeed = moveSpeed * deltaTime;
        if (isKeyDown(Keys::LeftShift)) {
            actualSpeed *= 2.0f;  // Sprint
        }

        movement.deltaPosition = direction * actualSpeed;

        return movement;
    }

    void InputServiceImpl::update() {
        // No per-frame state to update currently.
        // Mouse delta and scroll tracking would be added here if needed.
    }

    bool InputServiceImpl::isInputCapturedByUI() const {
        ImGuiIO& io = ImGui::GetIO();
        return io.WantCaptureKeyboard || io.WantCaptureMouse;
    }

    void InputServiceImpl::requestClose() {
        if (inputController) {
            inputController->requestClose();
        }
    }

}
