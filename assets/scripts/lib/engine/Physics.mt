// Physics - Static utility class for physics component operations
// Works with entity IDs (int) to control physics simulation
//
// Usage examples:
//   int self = Entity::self();
//   Physics::applyForce(self, 0.0, 100.0, 0.0);  // Apply upward force
//   Physics::applyImpulse(self, 10.0, 0.0, 0.0); // Instant velocity change
//   float[] vel = Physics::getLinearVelocity(self);
//   bool hasRb = Physics::hasRigidBody(self);
//
// ============================================
// Collision Callbacks (implement in your @Script class)
// ============================================
// These optional methods are called automatically when physics events occur:
//
//   // Called when this entity starts colliding with another solid entity
//   public function onCollisionEnter(int otherEntityId): void { }
//
//   // Called when this entity stops colliding with another solid entity
//   public function onCollisionExit(int otherEntityId): void { }
//
//   // Called when another entity enters this entity's trigger collider
//   public function onTriggerEnter(int otherEntityId): void { }
//
//   // Called when another entity exits this entity's trigger collider
//   public function onTriggerExit(int otherEntityId): void { }
//
// Example usage in a script:
//   @Script
//   public class PlayerController {
//       public function onCollisionEnter(int otherEntityId): void {
//           Log::info("Player collided with entity: " + otherEntityId);
//       }
//       public function onTriggerEnter(int otherEntityId): void {
//           Log::info("Player entered trigger zone");
//       }
//   }

public class Physics {
    // ============================================
    // Body Type Constants
    // ============================================
    public static const int BODY_STATIC = 0;
    public static const int BODY_DYNAMIC = 1;
    public static const int BODY_KINEMATIC = 2;

    // ============================================
    // Collider Shape Constants
    // ============================================
    public static const int SHAPE_BOX = 0;
    public static const int SHAPE_SPHERE = 1;
    public static const int SHAPE_CAPSULE = 2;
    public static const int SHAPE_CONVEX_MESH = 3;
    public static const int SHAPE_TRIANGLE_MESH = 4;

    public constructor() {
    }

    // ============================================
    // RigidBody Queries
    // ============================================

    // Check if entity has a RigidBody component
    public static function hasRigidBody(int entityId): bool {
        return _native_physics_hasRigidBody(entityId);
    }

    // Get body type (BODY_STATIC, BODY_DYNAMIC, BODY_KINEMATIC)
    public static function getBodyType(int entityId): int {
        return _native_physics_getBodyType(entityId);
    }

    // Get mass of the rigid body
    public static function getMass(int entityId): float {
        return _native_physics_getMass(entityId);
    }

    // Get linear damping (0.0 = no damping)
    public static function getLinearDamping(int entityId): float {
        return _native_physics_getLinearDamping(entityId);
    }

    // Get angular damping (0.0 = no damping)
    public static function getAngularDamping(int entityId): float {
        return _native_physics_getAngularDamping(entityId);
    }

    // Get current linear velocity as float[3] (x, y, z)
    public static function getLinearVelocity(int entityId): float[] {
        return _native_physics_getLinearVelocity(entityId);
    }

    // Get current angular velocity as float[3] (x, y, z)
    public static function getAngularVelocity(int entityId): float[] {
        return _native_physics_getAngularVelocity(entityId);
    }

    // ============================================
    // RigidBody Setters
    // ============================================

    // Set body type (BODY_STATIC, BODY_DYNAMIC, BODY_KINEMATIC)
    // Note: Changes take effect on next play mode start
    public static function setBodyType(int entityId, int type): void {
        _native_physics_setBodyType(entityId, type);
    }

    // Set mass of the rigid body
    // Note: Changes take effect on next play mode start
    public static function setMass(int entityId, float mass): void {
        _native_physics_setMass(entityId, mass);
    }

    // Set linear damping (0.0 = no damping)
    public static function setLinearDamping(int entityId, float damping): void {
        _native_physics_setLinearDamping(entityId, damping);
    }

    // Set angular damping (0.0 = no damping)
    public static function setAngularDamping(int entityId, float damping): void {
        _native_physics_setAngularDamping(entityId, damping);
    }

    // Set linear velocity directly
    public static function setLinearVelocity(int entityId, float x, float y, float z): void {
        _native_physics_setLinearVelocity(entityId, x, y, z);
    }

    // Set angular velocity directly
    public static function setAngularVelocity(int entityId, float x, float y, float z): void {
        _native_physics_setAngularVelocity(entityId, x, y, z);
    }

    // ============================================
    // Force and Impulse
    // ============================================

    // Apply continuous force (use in onUpdate for sustained effects)
    // Force is applied at the center of mass
    public static function applyForce(int entityId, float x, float y, float z): void {
        _native_physics_applyForce(entityId, x, y, z);
    }

    // Apply force at a specific world position (creates torque)
    public static function applyForceAtPosition(int entityId, float fx, float fy, float fz, float px, float py, float pz): void {
        _native_physics_applyForceAtPosition(entityId, fx, fy, fz, px, py, pz);
    }

    // Apply instant velocity change (use for jumps, explosions)
    public static function applyImpulse(int entityId, float x, float y, float z): void {
        _native_physics_applyImpulse(entityId, x, y, z);
    }

    // Apply torque (angular force)
    public static function applyTorque(int entityId, float x, float y, float z): void {
        _native_physics_applyTorque(entityId, x, y, z);
    }

    // ============================================
    // Physics Transform (direct body access)
    // ============================================

    // Get physics body position as float[3] (x, y, z)
    public static function getPosition(int entityId): float[] {
        return _native_physics_getPosition(entityId);
    }

    // Set physics body position directly (teleport)
    public static function setPosition(int entityId, float x, float y, float z): void {
        _native_physics_setPosition(entityId, x, y, z);
    }

    // Get physics body rotation as quaternion float[4] (x, y, z, w)
    public static function getRotation(int entityId): float[] {
        return _native_physics_getRotation(entityId);
    }

    // Set physics body rotation directly (quaternion x, y, z, w)
    public static function setRotation(int entityId, float x, float y, float z, float w): void {
        _native_physics_setRotation(entityId, x, y, z, w);
    }

    // ============================================
    // Collider Queries
    // ============================================

    // Check if entity has a Collider component
    public static function hasCollider(int entityId): bool {
        return _native_physics_hasCollider(entityId);
    }

    // Get collider shape type (SHAPE_BOX, SHAPE_SPHERE, etc.)
    public static function getColliderShape(int entityId): int {
        return _native_physics_getColliderShape(entityId);
    }

    // Get collider size as float[3] (half-extents for box, radius for sphere)
    public static function getColliderSize(int entityId): float[] {
        return _native_physics_getColliderSize(entityId);
    }

    // Get capsule height
    public static function getColliderHeight(int entityId): float {
        return _native_physics_getColliderHeight(entityId);
    }

    // Get collider offset from entity center as float[3]
    public static function getColliderOffset(int entityId): float[] {
        return _native_physics_getColliderOffset(entityId);
    }

    // Check if collider is a trigger (no collision response)
    public static function isTrigger(int entityId): bool {
        return _native_physics_isTrigger(entityId);
    }

    // Get collision layer (0-15)
    public static function getCollisionLayer(int entityId): int {
        return _native_physics_getCollisionLayer(entityId);
    }

    // Get friction coefficient
    public static function getFriction(int entityId): float {
        return _native_physics_getFriction(entityId);
    }

    // Get restitution (bounciness)
    public static function getRestitution(int entityId): float {
        return _native_physics_getRestitution(entityId);
    }

    // ============================================
    // Collider Setters
    // ============================================

    // Set collider size (half-extents for box, radius for sphere)
    public static function setColliderSize(int entityId, float x, float y, float z): void {
        _native_physics_setColliderSize(entityId, x, y, z);
    }

    // Set capsule height
    public static function setColliderHeight(int entityId, float height): void {
        _native_physics_setColliderHeight(entityId, height);
    }

    // Set whether collider is a trigger
    public static function setTrigger(int entityId, bool isTrigger): void {
        _native_physics_setTrigger(entityId, isTrigger);
    }

    // Set collision layer (0-15)
    public static function setCollisionLayer(int entityId, int layer): void {
        _native_physics_setCollisionLayer(entityId, layer);
    }

    // Set friction coefficient
    public static function setFriction(int entityId, float friction): void {
        _native_physics_setFriction(entityId, friction);
    }

    // Set restitution (bounciness)
    public static function setRestitution(int entityId, float restitution): void {
        _native_physics_setRestitution(entityId, restitution);
    }

    // ============================================
    // Raycasting
    // ============================================

    // Cast a ray and return hit information as float[]
    // Parameters: origin (x,y,z), direction (x,y,z), maxDistance
    // Returns: [hit(0/1), entityId, hitX, hitY, hitZ, normalX, normalY, normalZ, distance]
    // Returns [0] if no hit
    public static function raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance): float[] {
        return _native_physics_raycast(ox, oy, oz, dx, dy, dz, maxDistance);
    }

    // Check if two entities are overlapping
    public static function isOverlapping(int entityA, int entityB): bool {
        return _native_physics_isOverlapping(entityA, entityB);
    }

    // ============================================
    // World Settings
    // ============================================

    // Get current gravity as float[3] (x, y, z)
    public static function getGravity(): float[] {
        return _native_physics_getGravity();
    }

    // Set world gravity
    public static function setGravity(float x, float y, float z): void {
        _native_physics_setGravity(x, y, z);
    }
}
