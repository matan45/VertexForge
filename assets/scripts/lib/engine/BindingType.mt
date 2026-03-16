// BindingType - Constants for input binding types
// Use with InputAction::register() and InputAction::addBinding()
//
// Usage:
//   InputAction::register("Jump", BindingType::KEY, Key::SPACE);
//   InputAction::register("Shoot", BindingType::MOUSE_BUTTON, Mouse::LEFT);

public class BindingType {
    public static final int KEY = 0;
    public static final int MOUSE_BUTTON = 1;
}
