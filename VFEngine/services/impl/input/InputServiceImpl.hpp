#pragma once
#include "../../interfaces/input/IInputService.hpp"
#include "../../events/input/InputEvents.hpp"
#include <memory>

// Forward declaration - Window library
namespace window {
    class Window;
    class InputController;
}

namespace services {

    class InputServiceImpl : public IInputService {
    public:
        explicit InputServiceImpl(window::Window* window);
        ~InputServiceImpl() override;
        
        void registerEventHandlers() override;
        
        bool isKeyDown(int keyCode) const override;
        bool isKeyReleased(int keyCode) const override;
        bool isKeyPressed(int keyCode) const override;
        const std::vector<uint32_t>& getCharInput() const override;
        std::string getClipboardText() const override;
        void setClipboardText(const std::string& text) override;

        // Mouse State
        bool isMouseButtonDown(int button) const override;
        bool isMouseButtonPressed(int button) const override;
        bool isMouseButtonReleased(int button) const override;
        bool isDoubleClick(int button) const override;
        glm::vec2 getMousePosition() const override;
        glm::vec2 getMouseDelta() const override;
        glm::vec2 getScrollDelta() const override;

        // Camera Control Helpers
        CameraMovement calculateCameraMovement(
            float deltaTime,
            float moveSpeed,
            float mouseSensitivity,
            bool useMouseLook = true) const override;

        // Input State Management
        void update() override;
        bool isInputCapturedByUI() const override;

        void setKeyboardEnabled(bool enabled) override;
        void setMouseEnabled(bool enabled) override;
        void setCursorVisible(bool visible) override;
        bool isKeyboardEnabled() const override;
        bool isMouseEnabled() const override;
        bool isCursorVisible() const override;

    private:
       void publishInputNotifications();

       std::unique_ptr<window::InputController> inputController;
       window::Window* windowPtr = nullptr;
       bool keyboardEnabled = true;
       bool mouseEnabled = true;
       bool cursorVisible = true;
    };

}
