// InputContext - State-dependent input handling via context stacking
// Create named contexts, push/pop them to control which actions fire.
// A blocking context prevents input from reaching contexts below it on the stack.
//
// Usage:
//   InputContext::create("Gameplay", true);
//   InputContext::push("Gameplay");
//   InputContext::pop("Gameplay");
//   if (InputContext::isActive("Gameplay")) { ... }

public class InputContext {
    public constructor() {
    }

    // Create a named context. blocking=true prevents input to lower contexts.
    public static function create(String name, bool blocking): void {
        _native_inputcontext_create(name, blocking);
    }

    // Remove a context and all its stack state
    public static function remove(String name): void {
        _native_inputcontext_remove(name);
    }

    // Push a context onto the stack (activates it)
    public static function push(String name): void {
        _native_inputcontext_push(name);
    }

    // Pop a specific context from the stack (deactivates it)
    public static function pop(String name): void {
        _native_inputcontext_pop(name);
    }

    // Check if a context is currently active
    public static function isActive(String name): bool {
        return _native_inputcontext_isActive(name);
    }

    // Change whether a context blocks input to contexts below it
    public static function setBlocking(String name, bool blocking): void {
        _native_inputcontext_setBlocking(name, blocking);
    }
}
