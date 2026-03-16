#include "InputServiceImpl.hpp"
#include "../../Window/controllers/InputController.hpp"
#include "../../Window/window/Window.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/ApplicationEvents.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>

namespace services {

    InputServiceImpl::InputServiceImpl(window::Window* window)
        : inputController(std::make_unique<window::InputController>(window))
        , windowPtr(window) {}

    InputServiceImpl::~InputServiceImpl() = default;

    bool InputServiceImpl::isKeyDown(int keyCode) const {
        if (!inputController || !keyboardEnabled) return false;
        return inputController->isKeyDown(keyCode);
    }

    bool InputServiceImpl::isKeyReleased(int keyCode) const {
        if (!inputController || !keyboardEnabled) return false;
        return inputController->isKeyReleased(keyCode);
    }

    bool InputServiceImpl::isKeyPressed(int keyCode) const {
        if (!inputController || !keyboardEnabled) return false;
        return inputController->isKeyPressed(keyCode);
    }

    static const std::vector<uint32_t> emptyCharInput;

    const std::vector<uint32_t>& InputServiceImpl::getCharInput() const {
        if (!inputController) return emptyCharInput;
        return inputController->getCharInput();
    }

    std::string InputServiceImpl::getClipboardText() const {
        if (!inputController) return "";
        return inputController->getClipboardText();
    }

    void InputServiceImpl::setClipboardText(const std::string& text) {
        if (!inputController) return;
        inputController->setClipboardText(text);
    }

    bool InputServiceImpl::isMouseButtonDown(int button) const {
        if (!inputController || !mouseEnabled) return false;
        return inputController->isMouseButtonDown(button);
    }

    bool InputServiceImpl::isMouseButtonPressed(int button) const {
        if (!inputController || !mouseEnabled) return false;
        return inputController->isMouseButtonPressed(button);
    }

    bool InputServiceImpl::isMouseButtonReleased(int button) const {
        if (!inputController || !mouseEnabled) return false;
        return inputController->isMouseButtonReleased(button);
    }

    bool InputServiceImpl::isDoubleClick(int button) const {
        if (!inputController || !mouseEnabled) return false;
        return inputController->isDoubleClick(button);
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
        publishInputNotifications();
    }

    void InputServiceImpl::publishInputNotifications() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Gather current modifier state
        bool shiftDown = inputController->isKeyDown(340) || inputController->isKeyDown(344);
        bool ctrlDown = inputController->isKeyDown(341) || inputController->isKeyDown(345);
        bool altDown = inputController->isKeyDown(342) || inputController->isKeyDown(346);
        glm::vec2 mousePos = inputController->getMousePosition();

        // Key press notifications
        std::vector<int> keys;
        inputController->getJustPressedKeys(keys);
        for (int key : keys) {
            events::input::KeyPressedNotification notif;
            notif.keyCode = key;
            notif.shiftDown = shiftDown;
            notif.ctrlDown = ctrlDown;
            notif.altDown = altDown;
            notif.mouseX = mousePos.x;
            notif.mouseY = mousePos.y;
            dispatcher.publish(notif);
        }

        // Key release notifications
        keys.clear();
        inputController->getJustReleasedKeys(keys);
        for (int key : keys) {
            events::input::KeyReleasedNotification notif;
            notif.keyCode = key;
            notif.shiftDown = shiftDown;
            notif.ctrlDown = ctrlDown;
            notif.altDown = altDown;
            notif.mouseX = mousePos.x;
            notif.mouseY = mousePos.y;
            dispatcher.publish(notif);
        }

        // Mouse button press notifications
        std::vector<int> buttons;
        inputController->getJustPressedMouseButtons(buttons);
        for (int btn : buttons) {
            events::input::MouseButtonPressedNotification notif;
            notif.button = btn;
            notif.shiftDown = shiftDown;
            notif.ctrlDown = ctrlDown;
            notif.altDown = altDown;
            notif.mouseX = mousePos.x;
            notif.mouseY = mousePos.y;
            dispatcher.publish(notif);
        }

        // Mouse button release notifications
        buttons.clear();
        inputController->getJustReleasedMouseButtons(buttons);
        for (int btn : buttons) {
            events::input::MouseButtonReleasedNotification notif;
            notif.button = btn;
            notif.shiftDown = shiftDown;
            notif.ctrlDown = ctrlDown;
            notif.altDown = altDown;
            notif.mouseX = mousePos.x;
            notif.mouseY = mousePos.y;
            dispatcher.publish(notif);
        }
    }

    bool InputServiceImpl::isInputCapturedByUI() const {
        ImGuiIO& io = ImGui::GetIO();
        return io.WantCaptureKeyboard || io.WantCaptureMouse;
    }

    void InputServiceImpl::setKeyboardEnabled(bool enabled) {
        keyboardEnabled = enabled;
    }

    void InputServiceImpl::setMouseEnabled(bool enabled) {
        mouseEnabled = enabled;
    }

    void InputServiceImpl::setCursorVisible(bool visible) {
        cursorVisible = visible;
        if (windowPtr) {
            GLFWwindow* glfw = windowPtr->getWindowPtr();
            if (glfw) {
                glfwSetInputMode(glfw, GLFW_CURSOR,
                    visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
            }
        }
    }

    bool InputServiceImpl::isKeyboardEnabled() const { return keyboardEnabled; }
    bool InputServiceImpl::isMouseEnabled() const { return mouseEnabled; }
    bool InputServiceImpl::isCursorVisible() const { return cursorVisible; }

    void InputServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

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

        dispatcher.registerQueryHandler<events::input::GetScrollDeltaQuery>(
            [this](const events::input::GetScrollDeltaQuery&) {
                return getScrollDelta();
            });

        dispatcher.registerQueryHandler<events::input::IsKeyReleasedQuery>(
            [this](const events::input::IsKeyReleasedQuery& query) {
                return isKeyReleased(query.keyCode);
            });

        dispatcher.registerQueryHandler<events::input::IsMouseButtonPressedQuery>(
            [this](const events::input::IsMouseButtonPressedQuery& query) {
                return isMouseButtonPressed(query.button);
            });

        dispatcher.registerQueryHandler<events::input::IsMouseButtonReleasedQuery>(
            [this](const events::input::IsMouseButtonReleasedQuery& query) {
                return isMouseButtonReleased(query.button);
            });

        dispatcher.registerQueryHandler<events::input::IsDoubleClickQuery>(
            [this](const events::input::IsDoubleClickQuery& query) {
                return isDoubleClick(query.button);
            });

        dispatcher.registerQueryHandler<events::input::IsKeyPressedQuery>(
            [this](const events::input::IsKeyPressedQuery& query) {
                return isKeyPressed(query.keyCode);
            });

        dispatcher.registerQueryHandler<events::input::GetCharInputQuery>(
            [this](const events::input::GetCharInputQuery&) {
                return getCharInput();
            });

        dispatcher.registerQueryHandler<events::input::GetClipboardTextQuery>(
            [this](const events::input::GetClipboardTextQuery&) {
                return getClipboardText();
            });

        dispatcher.registerCommandHandler<events::input::SetClipboardTextCommand>(
            [this](const events::input::SetClipboardTextCommand& cmd) {
                setClipboardText(cmd.text);
            });

        dispatcher.registerCommandHandler<events::input::SetKeyboardEnabledCommand>(
            [this](const events::input::SetKeyboardEnabledCommand& cmd) {
                setKeyboardEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::input::SetMouseEnabledCommand>(
            [this](const events::input::SetMouseEnabledCommand& cmd) {
                setMouseEnabled(cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::input::SetCursorVisibleCommand>(
            [this](const events::input::SetCursorVisibleCommand& cmd) {
                setCursorVisible(cmd.visible);
            });

        dispatcher.registerQueryHandler<events::input::IsKeyboardEnabledQuery>(
            [this](const events::input::IsKeyboardEnabledQuery&) {
                return isKeyboardEnabled();
            });

        dispatcher.registerQueryHandler<events::input::IsMouseEnabledQuery>(
            [this](const events::input::IsMouseEnabledQuery&) {
                return isMouseEnabled();
            });

        dispatcher.registerQueryHandler<events::input::IsCursorVisibleQuery>(
            [this](const events::input::IsCursorVisibleQuery&) {
                return isCursorVisible();
            });
    }

}
