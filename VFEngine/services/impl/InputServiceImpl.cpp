#include "InputServiceImpl.hpp"
#include "../../Window/controllers/InputController.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/ApplicationEvents.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

namespace services {

    InputServiceImpl::InputServiceImpl(window::Window* window)
        : inputController(std::make_unique<window::InputController>(window)) {}

    InputServiceImpl::~InputServiceImpl() = default;

    bool InputServiceImpl::isKeyDown(int keyCode) const {
        if (!inputController) return false;
        return inputController->isKeyDown(keyCode);
    }

    bool InputServiceImpl::isKeyReleased(int keyCode) const {
        if (!inputController) return false;
        return inputController->isKeyReleased(keyCode);
    }

    bool InputServiceImpl::isMouseButtonDown(int button) const {
        if (!inputController) return false;
        return inputController->isMouseButtonDown(button);
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
        if (!inputController) return glm::vec2(0.0f);
        return inputController->getMouseDelta();
    }

    glm::vec2 InputServiceImpl::getScrollDelta() const {
        if (!inputController) return glm::vec2(0.0f);
        return inputController->getScrollDelta();
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
        if (!inputController) return;

        inputController->update();

        auto& dispatcher = events::EventDispatcher::instance();

        // Check for window resize
        if (inputController->isWindowResized()) {
            events::application::WindowResizedNotification notification;
            notification.width = inputController->getWindowWidth();
            notification.height = inputController->getWindowHeight();
            dispatcher.publish(notification);
            inputController->resetResizeFlag();
        }

        // Check for minimize state changes
        if (inputController->hasMinimizeStateChanged()) {
            if (inputController->isWindowMinimized()) {
                events::application::WindowMinimizedNotification notification;
                dispatcher.publish(notification);
            } else {
                events::application::WindowRestoredNotification notification;
                dispatcher.publish(notification);
            }
            inputController->resetMinimizeStateChanged();
        }

        // Check for focus state changes
        if (inputController->hasFocusStateChanged()) {
            events::application::WindowFocusedNotification notification;
            notification.focused = inputController->isWindowFocused();
            dispatcher.publish(notification);
            inputController->resetFocusStateChanged();
        }
    }

    bool InputServiceImpl::isInputCapturedByUI() const {
        ImGuiIO& io = ImGui::GetIO();
        return io.WantCaptureKeyboard || io.WantCaptureMouse;
    }

    void InputServiceImpl::requestClose() {
        if (inputController) {
            inputController->requestClose();
        }

        // Publish notification
        events::application::CloseRequestedNotification notification;
        events::EventDispatcher::instance().publish(notification);
    }

    void InputServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::application::CloseCommand>(
            [this](const events::application::CloseCommand&) {
                requestClose();
            });

        // Query handlers
        dispatcher.registerQueryHandler<events::input::IsKeyDownQuery>(
            [this](const events::input::IsKeyDownQuery& query) {
                return isKeyDown(query.keyCode);
            });

        dispatcher.registerQueryHandler<events::input::IsMouseButtonDownQuery>(
            [this](const events::input::IsMouseButtonDownQuery& query) {
                return isMouseButtonDown(query.button);
            });

        dispatcher.registerQueryHandler<events::input::GetMousePositionQuery>(
            [this](const events::input::GetMousePositionQuery&) {
                return getMousePosition();
            });

        dispatcher.registerQueryHandler<events::input::GetMouseDeltaQuery>(
            [this](const events::input::GetMouseDeltaQuery&) {
                return getMouseDelta();
            });
    }

}
