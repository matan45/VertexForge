// Input - Static utility class for keyboard and mouse input
// Provides real-time input state queries
//
// Usage examples:
//   if (Input::isKeyDown(Key::W)) { /* move forward */ }
//   if (Input::isKeyReleased(Key::SPACE)) { /* key not pressed */ }
//   if (Input::isMouseButtonDown(Mouse::LEFT)) { /* shooting */ }
//   if (Input::isDoubleClick(Mouse::LEFT)) { /* double-click action */ }
//   float mouseX = Input::getMouseX();
//   float mouseDeltaX = Input::getMouseDeltaX();

public class Input {
    public constructor() {
    }

    // ============================================
    // Keyboard
    // ============================================

    // Check if a key is currently held down
    public static function isKeyDown(int keyCode): bool {
        return _native_input_isKeyDown(keyCode);
    }

    // Check if a key is not pressed (released state)
    public static function isKeyReleased(int keyCode): bool {
        return _native_input_isKeyReleased(keyCode);
    }

    // ============================================
    // Mouse Buttons
    // ============================================

    // Check if a mouse button is currently held down
    public static function isMouseButtonDown(int button): bool {
        return _native_input_isMouseButtonDown(button);
    }

    // Check if a mouse button is not pressed (released state)
    public static function isMouseButtonReleased(int button): bool {
        return _native_input_isMouseButtonReleased(button);
    }

    // Check if a mouse button was double-clicked this frame
    public static function isDoubleClick(int button): bool {
        return _native_input_isDoubleClick(button);
    }

    // ============================================
    // Mouse Position
    // ============================================

    // Get mouse X position in window coordinates
    public static function getMouseX(): float {
        return _native_input_getMouseX();
    }

    // Get mouse Y position in window coordinates
    public static function getMouseY(): float {
        return _native_input_getMouseY();
    }

    // ============================================
    // Mouse Movement (Delta)
    // ============================================

    // Get mouse X movement since last frame
    public static function getMouseDeltaX(): float {
        return _native_input_getMouseDeltaX();
    }

    // Get mouse Y movement since last frame
    public static function getMouseDeltaY(): float {
        return _native_input_getMouseDeltaY();
    }

    // ============================================
    // Input Control
    // ============================================

    // Enable or disable keyboard input
    public static function setKeyboardEnabled(bool enabled): void {
        _native_input_setKeyboardEnabled(enabled);
    }

    // Enable or disable mouse input
    public static function setMouseEnabled(bool enabled): void {
        _native_input_setMouseEnabled(enabled);
    }

    // Show or hide the mouse cursor
    public static function setCursorVisible(bool visible): void {
        _native_input_setCursorVisible(visible);
    }

    // Check if keyboard input is enabled
    public static function isKeyboardEnabled(): bool {
        return _native_input_isKeyboardEnabled();
    }

    // Check if mouse input is enabled
    public static function isMouseEnabled(): bool {
        return _native_input_isMouseEnabled();
    }

    // Check if mouse cursor is visible
    public static function isCursorVisible(): bool {
        return _native_input_isCursorVisible();
    }
}
