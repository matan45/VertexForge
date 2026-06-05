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

    // Active viewport width in pixels.
    // Editor play mode: the ViewPort panel size. Standalone runtime: same as getWidth.
    public static function getViewportWidth(): int {
        return _native_window_getViewportWidth();
    }

    // Active viewport height in pixels.
    public static function getViewportHeight(): int {
        return _native_window_getViewportHeight();
    }
}
