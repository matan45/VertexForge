// Time - Static utility class for gameplay time control (slow-motion / pause)
// Wraps the engine Timer's scaled gameplay clock. Use this to read frame deltas
// and to drive slow-motion, fast-forward, or hard freeze of gameplay time.
//
// delta() / scale() / freeze() affect GAMEPLAY time only; the editor camera, UI,
// and render pacing always run on unscaled real time.
//
// Usage examples:
//   float dt = Time::delta();              // scaled game delta (0 while frozen)
//   float raw = Time::unscaledDelta();     // real wall-clock delta
//   Time::setScale(0.5);                   // half-speed slow motion
//   Time::setScale(2.0);                   // double-speed
//   Time::freeze();                        // hard-pause gameplay time
//   if (Time::isFrozen()) { Time::unfreeze(); }

public class Time {
    public constructor() {
    }

    // Scaled gameplay delta time for this frame, in seconds.
    // Returns 0 while frozen; multiplied by the current time scale otherwise.
    public static function delta(): float {
        return _native_time_delta();
    }

    // Raw (unscaled) wall-clock delta time for this frame, in seconds.
    // Unaffected by time scale or freeze.
    public static function unscaledDelta(): float {
        return _native_time_unscaledDelta();
    }

    // Current gameplay time scale (1.0 = normal, 0.5 = half-speed, 2.0 = double).
    public static function scale(): float {
        return _native_time_scale();
    }

    // Set the gameplay time scale. Values below 0 are clamped to 0.
    public static function setScale(float s): void {
        _native_time_setScale(s);
    }

    // Hard-freeze gameplay time (delta() returns 0 until unfrozen).
    public static function freeze(): void {
        _native_time_freeze();
    }

    // Resume gameplay time after a freeze.
    public static function unfreeze(): void {
        _native_time_unfreeze();
    }

    // Check whether gameplay time is currently frozen.
    public static function isFrozen(): bool {
        return _native_time_isFrozen();
    }
}
