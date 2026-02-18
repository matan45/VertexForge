// Animator - Static utility class for controlling entity animators
// Works with entity IDs (int) and parameter names (string)
//
// Usage examples:
//   int self = Entity::self();
//
//   // Check if entity has an animator
//   if (Animator::hasAnimator(self)) {
//       // Set parameters to trigger transitions
//       Animator::setFloat(self, "speed", 5.0);
//       Animator::setBool(self, "isJumping", true);
//       Animator::setTrigger(self, "attack");
//
//       // Query state
//       string currentState = Animator::getCurrentState(self);
//       bool isBlending = Animator::isBlending(self);
//
//       // Control playback
//       Animator::play(self);
//       Animator::pause(self);
//
//       // Force transition to a specific state
//       Animator::forceTransitionTo(self, "Idle", 0.25);
//   }

public class Animator {
    public constructor() {
    }

    // ============================================
    // Animator Queries
    // ============================================

    // Check if an entity has an animator attached
    public static function hasAnimator(int entityId): bool {
        return _native_animator_hasAnimator(entityId);
    }

    // Get the name of the current animation state
    public static function getCurrentState(int entityId): string {
        return _native_animator_getCurrentState(entityId);
    }

    // Check if the animator is currently playing
    public static function isPlaying(int entityId): bool {
        return _native_animator_isPlaying(entityId);
    }

    // Check if the animator is currently blending between states
    public static function isBlending(int entityId): bool {
        return _native_animator_isBlending(entityId);
    }

    // Get the normalized time (0-1) in the current animation state
    public static function getNormalizedTime(int entityId): float {
        return _native_animator_getNormalizedTime(entityId);
    }

    // ============================================
    // Parameter Setters
    // ============================================

    // Set a float parameter value
    public static function setFloat(int entityId, string paramName, float value): void {
        _native_animator_setFloat(entityId, paramName, value);
    }

    // Set an integer parameter value
    public static function setInt(int entityId, string paramName, int value): void {
        _native_animator_setInt(entityId, paramName, value);
    }

    // Set a boolean parameter value
    public static function setBool(int entityId, string paramName, bool value): void {
        _native_animator_setBool(entityId, paramName, value);
    }

    // Set a trigger parameter (one-shot, auto-resets after transition)
    public static function setTrigger(int entityId, string paramName): void {
        _native_animator_setTrigger(entityId, paramName);
    }

    // ============================================
    // Parameter Getters
    // ============================================

    // Get a float parameter value
    public static function getFloat(int entityId, string paramName): float {
        return _native_animator_getFloat(entityId, paramName);
    }

    // Get an integer parameter value
    public static function getInt(int entityId, string paramName): int {
        return _native_animator_getInt(entityId, paramName);
    }

    // Get a boolean parameter value
    public static function getBool(int entityId, string paramName): bool {
        return _native_animator_getBool(entityId, paramName);
    }

    // ============================================
    // Playback Control
    // ============================================

    // Start/resume animation playback
    public static function play(int entityId): void {
        _native_animator_play(entityId);
    }

    // Pause animation playback
    public static function pause(int entityId): void {
        _native_animator_pause(entityId);
    }

    // Stop animation playback and reset to beginning
    public static function stop(int entityId): void {
        _native_animator_stop(entityId);
    }

    // Reset the animator to initial state
    public static function reset(int entityId): void {
        _native_animator_reset(entityId);
    }

    // ============================================
    // Transition Control
    // ============================================

    // Force transition to a specific state by name
    // blendDuration: time in seconds to blend to the new state (default: 0.25)
    // Returns true if the state was found and transition started
    public static function forceTransitionTo(int entityId, string stateName, float blendDuration): bool {
        return _native_animator_forceTransitionTo(entityId, stateName, blendDuration);
    }

    // Force transition to a specific state with default blend duration
    public static function forceTransitionToImmediate(int entityId, string stateName): bool {
        return _native_animator_forceTransitionTo(entityId, stateName, 0.25);
    }

    // ============================================
    // Root Motion
    // ============================================

    // Enable or disable root motion for an entity
    // When enabled, the animation's root bone movement updates the entity transform
    // When disabled, scripts handle entity movement instead
    public static function setRootMotion(int entityId, bool enabled): void {
        _native_animator_setRootMotion(entityId, enabled);
    }

    // Check if root motion is enabled for an entity
    public static function getRootMotion(int entityId): bool {
        return _native_animator_getRootMotion(entityId);
    }
}
