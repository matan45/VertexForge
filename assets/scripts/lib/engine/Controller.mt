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
}
