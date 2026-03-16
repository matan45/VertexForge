#pragma once
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace services {

    // Key codes matching GLFW
    namespace Keys {
        constexpr int W = 87;
        constexpr int A = 65;
        constexpr int S = 83;
        constexpr int D = 68;
        constexpr int Q = 81;
        constexpr int E = 69;
        constexpr int Space = 32;
        constexpr int LeftShift = 340;
        constexpr int LeftControl = 341;
        constexpr int Escape = 256;
    }

    // Mouse buttons matching GLFW
    namespace MouseButtons {
        constexpr int Left = 0;
        constexpr int Right = 1;
        constexpr int Middle = 2;
    }

    // Input service interface - abstracts input handling
    // Presentation layer uses this instead of direct GLFW access
    class IInputService {
    public:
        virtual ~IInputService() = default;

        // Register CQRS event handlers
        virtual void registerEventHandlers() = 0;

        // ============================================
        // Keyboard State
        // ============================================

        // Check if a key is currently pressed
        virtual bool isKeyDown(int keyCode) const = 0;

        // Check if a key was just released this frame
        virtual bool isKeyReleased(int keyCode) const = 0;

        // Check if a key was just pressed this frame (rising-edge detection)
        virtual bool isKeyPressed(int keyCode) const = 0;

        // Get accumulated character input for this frame (Unicode codepoints)
        virtual const std::vector<uint32_t>& getCharInput() const = 0;

        // Clipboard
        virtual std::string getClipboardText() const = 0;
        virtual void setClipboardText(const std::string& text) = 0;

        // ============================================
        // Mouse State
        // ============================================

        // Check if a mouse button is currently pressed
        virtual bool isMouseButtonDown(int button) const = 0;

        // Check if a mouse button was just pressed this frame (rising-edge detection)
        virtual bool isMouseButtonPressed(int button) const = 0;

        // Check if a mouse button was just released this frame
        virtual bool isMouseButtonReleased(int button) const = 0;

        // Check if a mouse button was double-clicked this frame
        virtual bool isDoubleClick(int button) const = 0;

        // Get current mouse position in window coordinates
        virtual glm::vec2 getMousePosition() const = 0;

        // Get mouse movement since last frame
        virtual glm::vec2 getMouseDelta() const = 0;

        // Get mouse scroll delta
        virtual glm::vec2 getScrollDelta() const = 0;

        // ============================================
        // Camera Control Helpers
        // ============================================

        // Calculate camera movement based on current input state
        // Returns position delta and rotation delta
        virtual CameraMovement calculateCameraMovement(
            float deltaTime,
            float moveSpeed,
            float mouseSensitivity,
            bool useMouseLook = true) const = 0;

        // ============================================
        // Input State Management
        // ============================================

        // Update input state (called once per frame)
        virtual void update() = 0;

        // Check if input is currently captured by ImGui
        virtual bool isInputCapturedByUI() const = 0;

    };

}
