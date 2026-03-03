// Controller - Static utility class for character movement control
// Works with the Controller component to provide movement execution.
// Input handling (player or AI) is done in your scripts - this class
// provides the movement interface that both player and AI scripts use.
//
// Usage examples:
//   int self = Entity::self();
//   Controller::setMoveInput(self, new Vec3f(1.0, 0.0, 0.0));  // move right
//   Controller::jump(self);
//   Controller::moveTo(self, new Vec3f(10.0, 0.0, 5.0));  // pathfind to target
//   if (Controller::hasReachedDestination(self)) { /* arrived */ }
//
// Note: Entities must have a Controller component for these functions to work.

import * from "../math/Vec3f.mt";

public class Controller {

    public constructor() {
    }

    // ============================================
    // Movement Commands
    // ============================================

    // Set movement direction for this frame (normalized)
    // Must be called every frame - resets automatically after processing
    public static function setMoveInput(int entityId, Vec3f input): void {
        _native_controller_setMoveInput(entityId, input.x, input.y, input.z);
    }

    // Request a jump this frame
    public static function jump(int entityId): void {
        _native_controller_setJump(entityId, true);
    }

    // Set sprint state for this frame
    public static function setSprint(int entityId, bool wantsSprint): void {
        _native_controller_setSprint(entityId, wantsSprint);
    }

    // Move entity toward a world position
    // Uses navmesh if available, otherwise direct movement
    // Persists until destination is reached or stop() is called
    public static function moveTo(int entityId, Vec3f target): bool {
        return _native_controller_moveTo(entityId, target.x, target.y, target.z);
    }

    // Stop all movement for an entity
    public static function stop(int entityId): void {
        _native_controller_stopMovement(entityId);
    }

    // ============================================
    // State Queries
    // ============================================

    // Check if entity has reached its moveTo destination
    public static function hasReachedDestination(int entityId): bool {
        return _native_controller_hasReachedDestination(entityId);
    }

    // Get distance from entity to a world position
    public static function getDistanceTo(int entityId, Vec3f target): float {
        return _native_controller_getDistanceTo(entityId, target.x, target.y, target.z);
    }

    // ============================================
    // Speed Control
    // ============================================

    // Get current move speed
    public static function getMoveSpeed(int entityId): float {
        return _native_controller_getMoveSpeed(entityId);
    }

    // Set move speed
    public static function setMoveSpeed(int entityId, float speed): void {
        _native_controller_setMoveSpeed(entityId, speed);
    }

    // Get current jump force
    public static function getJumpForce(int entityId): float {
        return _native_controller_getJumpForce(entityId);
    }

    // Set jump force
    public static function setJumpForce(int entityId, float force): void {
        _native_controller_setJumpForce(entityId, force);
    }

    // Get current sprint multiplier
    public static function getSprintMultiplier(int entityId): float {
        return _native_controller_getSprintMultiplier(entityId);
    }

    // Set sprint multiplier
    public static function setSprintMultiplier(int entityId, float multiplier): void {
        _native_controller_setSprintMultiplier(entityId, multiplier);
    }

    // Get arrival distance threshold for moveTo
    public static function getArrivalDistance(int entityId): float {
        return _native_controller_getArrivalDistance(entityId);
    }

    // Set arrival distance threshold for moveTo
    public static function setArrivalDistance(int entityId, float distance): void {
        _native_controller_setArrivalDistance(entityId, distance);
    }

    // ============================================
    // Ground State
    // ============================================

    // Check if entity is on the ground (must be set by collision scripts)
    public static function isGrounded(int entityId): bool {
        return _native_controller_isGrounded(entityId);
    }

    // Set grounded state (call from collision listener scripts)
    public static function setGrounded(int entityId, bool grounded): void {
        _native_controller_setGrounded(entityId, grounded);
    }

    // ============================================
    // Locomotion State
    // ============================================

    // Get current locomotion state as string (auto-derived or script-set)
    public static function getLocomotionState(int entityId): string {
        return _native_controller_getLocomotionState(entityId);
    }

    // Set locomotion state to any custom string (overrides auto-derivation for this frame)
    public static function setLocomotionState(int entityId, string state): void {
        _native_controller_setLocomotionState(entityId, state);
    }

    // Get current horizontal speed (magnitude of velocity on XZ plane)
    public static function getCurrentSpeed(int entityId): float {
        return _native_controller_getCurrentSpeed(entityId);
    }

    // Get current vertical velocity (positive = going up, negative = falling)
    public static function getVerticalVelocity(int entityId): float {
        return _native_controller_getVerticalVelocity(entityId);
    }

    // ============================================
    // Locomotion Settings
    // ============================================

    // Get acceleration rate (how fast the character reaches target speed)
    public static function getAcceleration(int entityId): float {
        return _native_controller_getAcceleration(entityId);
    }

    // Set acceleration rate
    public static function setAcceleration(int entityId, float value): void {
        _native_controller_setAcceleration(entityId, value);
    }

    // Get deceleration rate (how fast the character stops)
    public static function getDeceleration(int entityId): float {
        return _native_controller_getDeceleration(entityId);
    }

    // Set deceleration rate
    public static function setDeceleration(int entityId, float value): void {
        _native_controller_setDeceleration(entityId, value);
    }

    // Get rotation speed in degrees per second
    public static function getRotationSpeed(int entityId): float {
        return _native_controller_getRotationSpeed(entityId);
    }

    // Set rotation speed in degrees per second
    public static function setRotationSpeed(int entityId, float value): void {
        _native_controller_setRotationSpeed(entityId, value);
    }

    // Get air control factor (0.0 = no air control, 1.0 = full control)
    public static function getAirControl(int entityId): float {
        return _native_controller_getAirControl(entityId);
    }

    // Set air control factor
    public static function setAirControl(int entityId, float value): void {
        _native_controller_setAirControl(entityId, value);
    }

    // Get walk/run speed threshold (below = walk, above = run)
    public static function getWalkRunThreshold(int entityId): float {
        return _native_controller_getWalkRunThreshold(entityId);
    }

    // Set walk/run speed threshold
    public static function setWalkRunThreshold(int entityId, float value): void {
        _native_controller_setWalkRunThreshold(entityId, value);
    }
}
