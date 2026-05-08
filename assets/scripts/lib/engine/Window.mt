// Window - Static utility class for application window queries
//
// Usage examples:
//   int w = Window::getWidth();
//   int h = Window::getHeight();

public class Window {
    public constructor() {
    }

    // Current window width in pixels
    public static function getWidth(): int {
        return _native_window_getWidth();
    }

    // Current window height in pixels
    public static function getHeight(): int {
        return _native_window_getHeight();
    }
}
