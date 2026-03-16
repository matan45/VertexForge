#include "InputController.hpp"
#include "../window/Window.hpp"
#include <GLFW/glfw3.h>

namespace window
{
    void InputController::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        auto* controller = getControllerForWindow(window);
        if (controller)
        {
            // Chain to previous callback (e.g. ImGui) first
            if (controller->previousScrollCallback)
            {
                controller->previousScrollCallback(window, xoffset, yoffset);
            }
            controller->onScroll(xoffset, yoffset);
        }
    }

    void InputController::charCallback(GLFWwindow* window, unsigned int codepoint)
    {
        auto* controller = getControllerForWindow(window);
        if (controller)
        {
            // Chain to previous callback (e.g. ImGui) first
            if (controller->previousCharCallback)
            {
                controller->previousCharCallback(window, codepoint);
            }
            controller->charBuffer.push_back(static_cast<uint32_t>(codepoint));
        }
    }

    InputController* InputController::getControllerForWindow(GLFWwindow* window)
    {
        auto it = controllerRegistry.find(window);
        return (it != controllerRegistry.end()) ? it->second : nullptr;
    }

    InputController::InputController(Window* window)
        : window(window)
          , glfwWindow(window ? window->getWindowPtr() : nullptr)
    {
        if (glfwWindow)
        {
            // Register in static map (doesn't conflict with Window's user pointer)
            controllerRegistry[glfwWindow] = this;
            // Save previous callback (e.g. ImGui's) for chaining
            previousScrollCallback = glfwSetScrollCallback(glfwWindow, scrollCallback);
            previousCharCallback = glfwSetCharCallback(glfwWindow, charCallback);
        }
    }

    InputController::~InputController()
    {
        if (glfwWindow)
        {
            glfwSetScrollCallback(glfwWindow, previousScrollCallback);
            glfwSetCharCallback(glfwWindow, previousCharCallback);
            controllerRegistry.erase(glfwWindow);
        }
    }

    bool InputController::isKeyDown(int keyCode) const
    {
        if (!glfwWindow || keyCode < 0 || keyCode > GLFW_KEY_LAST) return false;
        return glfwGetKey(glfwWindow, keyCode) == GLFW_PRESS;
    }

    bool InputController::isKeyReleased(int keyCode) const
    {
        if (!glfwWindow || keyCode < 0 || keyCode > GLFW_KEY_LAST) return false;
        return glfwGetKey(glfwWindow, keyCode) == GLFW_RELEASE;
    }

    bool InputController::isKeyPressed(int keyCode) const
    {
        if (keyCode < 0 || keyCode >= MAX_KEYS) return false;
        return keyPressed[keyCode];
    }

    const std::vector<uint32_t>& InputController::getCharInput() const
    {
        return frameCharBuffer;
    }

    bool InputController::isMouseButtonDown(int button) const
    {
        if (!glfwWindow || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
        return glfwGetMouseButton(glfwWindow, button) == GLFW_PRESS;
    }

    bool InputController::isMouseButtonReleased(int button) const
    {
        if (!glfwWindow || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
        return glfwGetMouseButton(glfwWindow, button) == GLFW_RELEASE;
    }

    bool InputController::isDoubleClick(int button) const
    {
        if (button < 0 || button >= MAX_MOUSE_BUTTONS) return false;
        return doubleClickDetected[button];
    }

    glm::vec2 InputController::getMousePosition() const
    {
        if (!glfwWindow) return glm::vec2(0.0f);

        double xpos, ypos;
        glfwGetCursorPos(glfwWindow, &xpos, &ypos);
        return glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos));
    }

    void InputController::getCursorPos(double& xpos, double& ypos) const
    {
        if (!glfwWindow)
        {
            xpos = 0.0;
            ypos = 0.0;
            return;
        }
        glfwGetCursorPos(glfwWindow, &xpos, &ypos);
    }

    glm::vec2 InputController::getMouseDelta() const
    {
        return mouseDelta;
    }

    glm::vec2 InputController::getScrollDelta() const
    {
        return frameScrollDelta;
    }

    void InputController::update()
    {
        // Calculate mouse delta
        glm::vec2 currentPos = getMousePosition();

        if (firstMouseUpdate)
        {
            lastMousePos = currentPos;
            firstMouseUpdate = false;
            mouseDelta = glm::vec2(0.0f);
        }
        else
        {
            mouseDelta = currentPos - lastMousePos;
            lastMousePos = currentPos;
        }

        // Save scroll delta for this frame, then reset accumulator
        frameScrollDelta = scrollDelta;
        scrollDelta = glm::vec2(0.0f);

        // Swap character input buffer (same pattern as scroll delta)
        frameCharBuffer.swap(charBuffer);
        charBuffer.clear();

        // Key pressed/released edge detection
        for (int key = 0; key < MAX_KEYS; ++key)
        {
            bool down = (glfwWindow && glfwGetKey(glfwWindow, key) == GLFW_PRESS);
            keyPressed[key] = down && !wasKeyDown[key];
            keyReleased[key] = !down && wasKeyDown[key];
            wasKeyDown[key] = down;
        }

        // Mouse button pressed/released edge detection + double-click detection
        double currentTime = glfwGetTime();
        for (int button = 0; button < MAX_MOUSE_BUTTONS; ++button)
        {
            doubleClickDetected[button] = false;

            bool isDown = isMouseButtonDown(button);
            mouseButtonPressed[button] = isDown && !wasButtonDown[button];
            mouseButtonReleased[button] = !isDown && wasButtonDown[button];

            // Detect button press (transition from up to down)
            if (isDown && !wasButtonDown[button])
            {
                double timeSinceLastClick = currentTime - lastClickTime[button];
                float distance = glm::length(currentPos - lastClickPos[button]);

                if (timeSinceLastClick < DOUBLE_CLICK_TIME && distance < DOUBLE_CLICK_DISTANCE)
                {
                    doubleClickDetected[button] = true;
                    lastClickTime[button] = 0.0;
                }
                else
                {
                    lastClickTime[button] = currentTime;
                    lastClickPos[button] = currentPos;
                }
            }

            wasButtonDown[button] = isDown;
        }
    }

    std::string InputController::getClipboardText() const
    {
        if (!glfwWindow) return "";
        const char* text = glfwGetClipboardString(glfwWindow);
        return text ? std::string(text) : std::string();
    }

    void InputController::setClipboardText(const std::string& text)
    {
        if (!glfwWindow) return;
        glfwSetClipboardString(glfwWindow, text.c_str());
    }

    bool InputController::isMouseButtonPressed(int button) const
    {
        if (button < 0 || button >= MAX_MOUSE_BUTTONS) return false;
        return mouseButtonPressed[button];
    }

    void InputController::getJustPressedKeys(std::vector<int>& outKeys) const
    {
        for (int key = 0; key < MAX_KEYS; ++key)
        {
            if (keyPressed[key]) outKeys.push_back(key);
        }
    }

    void InputController::getJustReleasedKeys(std::vector<int>& outKeys) const
    {
        for (int key = 0; key < MAX_KEYS; ++key)
        {
            if (keyReleased[key]) outKeys.push_back(key);
        }
    }

    void InputController::getJustPressedMouseButtons(std::vector<int>& outButtons) const
    {
        for (int button = 0; button < MAX_MOUSE_BUTTONS; ++button)
        {
            if (mouseButtonPressed[button]) outButtons.push_back(button);
        }
    }

    void InputController::getJustReleasedMouseButtons(std::vector<int>& outButtons) const
    {
        for (int button = 0; button < MAX_MOUSE_BUTTONS; ++button)
        {
            if (mouseButtonReleased[button]) outButtons.push_back(button);
        }
    }

    void InputController::onScroll(double xoffset, double yoffset)
    {
        scrollDelta.x += static_cast<float>(xoffset);
        scrollDelta.y += static_cast<float>(yoffset);
    }
}
