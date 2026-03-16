// InputAction - Named action bindings for input abstraction
// Register actions with default bindings, then query by name.
// Supports multiple keys/buttons per action, runtime rebinding, and persistence.
//
// Usage:
//   InputAction::register("Jump", BindingType::KEY, Key::SPACE);
//   InputAction::addBinding("Jump", BindingType::KEY, Key::W);
//   InputAction::registerCombo("Sprint", BindingType::KEY, Key::W, true, false, false); // Shift+W
//   if (InputAction::isPressed("Jump")) { /* jump! */ }
//   InputAction::save("config/default.vfInputMapping");

public class InputAction {
    public constructor() {
    }

    // Check if any binding for this action is currently held down
    public static function isDown(String actionName): bool {
        return _native_inputaction_isDown(actionName);
    }

    // Check if any binding for this action was just pressed this frame
    public static function isPressed(String actionName): bool {
        return _native_inputaction_isPressed(actionName);
    }

    // Check if any binding for this action was just released this frame
    public static function isReleased(String actionName): bool {
        return _native_inputaction_isReleased(actionName);
    }

    // Register a new action with a single default binding
    public static function register(String actionName, int bindingType, int code): void {
        _native_inputaction_register(actionName, bindingType, code);
    }

    // Register a new action in a specific input context
    public static function registerInContext(String actionName, String context, int bindingType, int code): void {
        _native_inputaction_registerInContext(actionName, context, bindingType, code);
    }

    // Register a new action with a combo binding (e.g., Shift+W)
    public static function registerCombo(String actionName, int bindingType, int code, bool shift, bool ctrl, bool alt): void {
        _native_inputaction_register(actionName, bindingType, code, shift, ctrl, alt);
    }

    // Add an additional binding to an existing action
    public static function addBinding(String actionName, int bindingType, int code): void {
        _native_inputaction_addBinding(actionName, bindingType, code);
    }

    // Add a combo binding to an existing action (e.g., Shift+W)
    public static function addComboBinding(String actionName, int bindingType, int code, bool shift, bool ctrl, bool alt): void {
        _native_inputaction_addBinding(actionName, bindingType, code, shift, ctrl, alt);
    }

    // Remove a specific binding from an action
    public static function removeBinding(String actionName, int bindingType, int code): void {
        _native_inputaction_removeBinding(actionName, bindingType, code);
    }

    // Remove a specific combo binding from an action
    public static function removeComboBinding(String actionName, int bindingType, int code, bool shift, bool ctrl, bool alt): void {
        _native_inputaction_removeBinding(actionName, bindingType, code, shift, ctrl, alt);
    }

    // Reset an action back to its default bindings
    public static function resetBindings(String actionName): void {
        _native_inputaction_resetBindings(actionName);
    }

    // Save custom bindings to a JSON file
    public static function save(String filePath): bool {
        return _native_inputaction_save(filePath);
    }

    // Load custom bindings from a JSON file
    public static function load(String filePath): bool {
        return _native_inputaction_load(filePath);
    }

    // Consume an action so lower-priority scripts don't see it this frame
    public static function consume(String actionName): void {
        _native_inputaction_consume(actionName);
    }

    // Check if an action has been consumed this frame
    public static function isConsumed(String actionName): bool {
        return _native_inputaction_isConsumed(actionName);
    }
}
