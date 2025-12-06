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
        return currentMousePosition;
    }

    glm::vec2 InputServiceImpl::getMouseDelta() const {
        return mouseDelta;
    }

    glm::vec2 InputServiceImpl::getScrollDelta() const {
        return scrollDelta;
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
        if (useMouseLook) {
            movement.deltaRotation.x = mouseDelta.x * mouseSensitivity;  // yaw
            movement.deltaRotation.y = mouseDelta.y * mouseSensitivity;  // pitch
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
        if (isKeyDown(Keys::E) || isKeyDown(Keys::Space)) {
            direction.y += 1.0f;  // Up
        }
        if (isKeyDown(Keys::Q) || isKeyDown(Keys::LeftControl)) {
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
        if (!inputController) return;

        // Get current mouse position via Core controller
        double xpos, ypos;
        inputController->getCursorPos(xpos, ypos);

        currentMousePosition = glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));

        // Calculate delta
        if (isFirstUpdate) {
            lastMousePosition = currentMousePosition;
            isFirstUpdate = false;
        }

        mouseDelta = currentMousePosition - lastMousePosition;
        lastMousePosition = currentMousePosition;

        // Reset scroll delta (would need GLFW scroll callback for proper implementation)
        scrollDelta = glm::vec2(0.0f);
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
