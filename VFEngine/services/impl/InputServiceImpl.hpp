#pragma once
#include "../interfaces/IInputService.hpp"

// Forward declaration - Core layer
namespace controllers {
    class InputController;
}

namespace services {

    class InputServiceImpl : public IInputService {
    public:
        explicit InputServiceImpl(controllers::InputController* inputController);
        ~InputServiceImpl() override = default;

        // Keyboard State
        bool isKeyDown(int keyCode) const override;
        bool isKeyPressed(int keyCode) const override;
        bool isKeyReleased(int keyCode) const override;

        // Mouse State
        bool isMouseButtonDown(int button) const override;
        bool isMouseButtonPressed(int button) const override;
        bool isMouseButtonReleased(int button) const override;
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

        // Application Control
        void requestClose() override;

    private:
        controllers::InputController* inputController;

        // Previous frame state for detecting press/release
        glm::vec2 lastMousePosition{ 0.0f, 0.0f };
        glm::vec2 currentMousePosition{ 0.0f, 0.0f };
        glm::vec2 mouseDelta{ 0.0f, 0.0f };
        glm::vec2 scrollDelta{ 0.0f, 0.0f };

        bool isFirstUpdate = true;
    };

}
