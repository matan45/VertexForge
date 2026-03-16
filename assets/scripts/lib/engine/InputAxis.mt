// InputAxis - Compose discrete action inputs into analog axis values
// 1D axes map two actions to a [-1, 1] float.
// 2D axes map four actions to a normalized vec2 (prevents diagonal speed boost).
// Axes reference actions (not raw keys), so rebinding keys automatically updates axis behavior.
//
// Usage:
//   InputAxis::register1D("Vertical", "MoveForward", "MoveBackward");
//   InputAxis::register2D("Movement", "MoveForward", "MoveBackward", "MoveLeft", "MoveRight");
//   float v = InputAxis::getValue1D("Vertical");
//   float mx = InputAxis::getValue2DX("Movement");
//   float my = InputAxis::getValue2DY("Movement");

public class InputAxis {
    public constructor() {
    }

    // Get the current value of a 1D axis: -1, 0, or +1
    public static function getValue1D(String axisName): float {
        return _native_inputaxis_getValue1D(axisName);
    }

    // Get the X component of a 2D axis value
    public static function getValue2DX(String axisName): float {
        return _native_inputaxis_getValue2DX(axisName);
    }

    // Get the Y component of a 2D axis value
    public static function getValue2DY(String axisName): float {
        return _native_inputaxis_getValue2DY(axisName);
    }

    // Register a 1D axis from two actions (positive = +1, negative = -1)
    public static function register1D(String axisName, String positiveAction, String negativeAction): void {
        _native_inputaxis_register1D(axisName, positiveAction, negativeAction);
    }

    // Register a 2D axis from four actions, normalized to prevent diagonal speed boost
    public static function register2D(String axisName, String upAction, String downAction, String leftAction, String rightAction): void {
        _native_inputaxis_register2D(axisName, upAction, downAction, leftAction, rightAction, true);
    }

    // Register a 2D axis from four actions, without normalization
    public static function register2DRaw(String axisName, String upAction, String downAction, String leftAction, String rightAction): void {
        _native_inputaxis_register2D(axisName, upAction, downAction, leftAction, rightAction, false);
    }

    // Unregister a 1D axis
    public static function unregister1D(String axisName): void {
        _native_inputaxis_unregister1D(axisName);
    }

    // Unregister a 2D axis
    public static function unregister2D(String axisName): void {
        _native_inputaxis_unregister2D(axisName);
    }
}
