// Physics - Static utility class for physics component operations
// Works with entity IDs (int) to control physics simulation
//
// Usage examples:
//   int self = Entity::self();
//   Physics::applyForce(self, new Vec3f(0.0, 100.0, 0.0));  // Apply upward force
//   Physics::applyImpulse(self, new Vec3f(10.0, 0.0, 0.0)); // Instant velocity change
//   Vec3f vel = Physics::getLinearVelocity(self);
//   bool hasRb = Physics::hasRigidBody(self);
//
// ============================================
// Collision/Trigger Callbacks
// ============================================
// Implement ICollisionListener and/or ITriggerListener interfaces to receive physics events:
//
//   import engine::ICollisionListener;
//   import engine::ITriggerListener;
//
//   @Script
//   public class PlayerController implements ICollisionListener, ITriggerListener {
//       @Override
//       public function onCollisionEnter(int otherEntityId): void {
//           Log::info("Player collided with entity: " + otherEntityId);
//       }
//       @Override
//       public function onCollisionExit(int otherEntityId): void { }
//       @Override
//       public function onTriggerEnter(int otherEntityId): void {
//           Log::info("Player entered trigger zone");
//       }
//       @Override
//       public function onTriggerExit(int otherEntityId): void { }
//   }
//
// You can also implement just the callbacks you need without using interfaces (duck typing).

import * from "../math/Vec3f.mt";
import * from "../math/Quaternion.mt";
import * from "RaycastHit.mt";

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

    // Get current linear velocity
    public static function getLinearVelocity(int entityId): Vec3f {
        float[] v = _native_physics_getLinearVelocity(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Get current angular velocity
    public static function getAngularVelocity(int entityId): Vec3f {
        float[] v = _native_physics_getAngularVelocity(entityId);
        return new Vec3f(v[0], v[1], v[2]);
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
    public static function setLinearVelocity(int entityId, Vec3f velocity): void {
        _native_physics_setLinearVelocity(entityId, velocity.x, velocity.y, velocity.z);
    }

    // Set angular velocity directly
    public static function setAngularVelocity(int entityId, Vec3f velocity): void {
        _native_physics_setAngularVelocity(entityId, velocity.x, velocity.y, velocity.z);
    }

    // ============================================
    // Force and Impulse
    // ============================================

    // Apply continuous force (use in onUpdate for sustained effects)
    // Force is applied at the center of mass
    public static function applyForce(int entityId, Vec3f force): void {
        _native_physics_applyForce(entityId, force.x, force.y, force.z);
    }

    // Apply force at a specific world position (creates torque)
    public static function applyForceAtPosition(int entityId, Vec3f force, Vec3f position): void {
        _native_physics_applyForceAtPosition(entityId, force.x, force.y, force.z, position.x, position.y, position.z);
    }

    // Apply instant velocity change (use for jumps, explosions, bullets)
    public static function applyImpulse(int entityId, Vec3f impulse): void {
        _native_physics_applyImpulse(entityId, impulse.x, impulse.y, impulse.z);
    }

    // Apply torque (angular force)
    public static function applyTorque(int entityId, Vec3f torque): void {
        _native_physics_applyTorque(entityId, torque.x, torque.y, torque.z);
    }

    // ============================================
    // Physics Transform (direct body access)
    // ============================================

    // Get physics body position
    public static function getPosition(int entityId): Vec3f {
        float[] v = _native_physics_getPosition(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Set physics body position directly (teleport)
    public static function setPosition(int entityId, Vec3f position): void {
        _native_physics_setPosition(entityId, position.x, position.y, position.z);
    }

    // Get physics body rotation
    public static function getRotation(int entityId): Quaternion {
        float[] q = _native_physics_getRotation(entityId);
        return new Quaternion(q[0], q[1], q[2], q[3]);
    }

    // Set physics body rotation directly
    public static function setRotation(int entityId, Quaternion rotation): void {
        _native_physics_setRotation(entityId, rotation.x, rotation.y, rotation.z, rotation.w);
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

    // Get collider size (half-extents for box, radius for sphere)
    public static function getColliderSize(int entityId): Vec3f {
        float[] v = _native_physics_getColliderSize(entityId);
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Get capsule height
    public static function getColliderHeight(int entityId): float {
        return _native_physics_getColliderHeight(entityId);
    }

    // Get collider offset from entity center
    public static function getColliderOffset(int entityId): Vec3f {
        float[] v = _native_physics_getColliderOffset(entityId);
        return new Vec3f(v[0], v[1], v[2]);
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
    public static function setColliderSize(int entityId, Vec3f size): void {
        _native_physics_setColliderSize(entityId, size.x, size.y, size.z);
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
    // Returns: [hit(0/1), entityId, hitX, hitY, hitZ, normalX, normalY, normalZ, distance]
    // Returns [0] if no hit
    public static function raycast(Vec3f origin, Vec3f direction, float maxDistance): float[] {
        return _native_physics_raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance);
    }

    // Cast a ray filtered by collision layer names (comma-separated, e.g. "Dynamic,Sensor")
    public static function raycast(Vec3f origin, Vec3f direction, float maxDistance, string layers): float[] {
        return _native_physics_raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance, layers);
    }

    // Cast a ray and return a RaycastHit object
    // Returns a RaycastHit with hit=false if nothing was hit
    public static function raycastHit(Vec3f origin, Vec3f direction, float maxDistance): RaycastHit {
        float[] raw = _native_physics_raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance);
        if (raw[0] < 0.5) {
            return new RaycastHit();
        }
        return new RaycastHit(
            true,
            raw[1] as int,
            new Vec3f(raw[2], raw[3], raw[4]),
            new Vec3f(raw[5], raw[6], raw[7]),
            raw[8]
        );
    }

    // Cast a ray filtered by collision layer names and return a RaycastHit object
    // layers: comma-separated layer names (e.g. "Dynamic,Kinematic")
    public static function raycastHit(Vec3f origin, Vec3f direction, float maxDistance, string layers): RaycastHit {
        float[] raw = _native_physics_raycast(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance, layers);
        if (raw[0] < 0.5) {
            return new RaycastHit();
        }
        return new RaycastHit(
            true,
            raw[1] as int,
            new Vec3f(raw[2], raw[3], raw[4]),
            new Vec3f(raw[5], raw[6], raw[7]),
            raw[8]
        );
    }

    // Cast a ray and return ALL hits along the ray, sorted by distance (nearest first)
    // Returns empty array if nothing was hit
    public static function raycastAll(Vec3f origin, Vec3f direction, float maxDistance): RaycastHit[] {
        float[] raw = _native_physics_raycastAll(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance);
        int count = raw[0] as int;
        RaycastHit[] hits = new RaycastHit[count];
        for (int i = 0; i < count; i = i + 1) {
            int base = 1 + i * 8;
            hits[i] = new RaycastHit(
                true,
                raw[base] as int,
                new Vec3f(raw[base + 1], raw[base + 2], raw[base + 3]),
                new Vec3f(raw[base + 4], raw[base + 5], raw[base + 6]),
                raw[base + 7]
            );
        }
        return hits;
    }

    // Cast a ray filtered by collision layer names and return ALL hits, sorted by distance
    // layers: comma-separated layer names (e.g. "Dynamic,Sensor")
    public static function raycastAll(Vec3f origin, Vec3f direction, float maxDistance, string layers): RaycastHit[] {
        float[] raw = _native_physics_raycastAll(origin.x, origin.y, origin.z, direction.x, direction.y, direction.z, maxDistance, layers);
        int count = raw[0] as int;
        RaycastHit[] hits = new RaycastHit[count];
        for (int i = 0; i < count; i = i + 1) {
            int base = 1 + i * 8;
            hits[i] = new RaycastHit(
                true,
                raw[base] as int,
                new Vec3f(raw[base + 1], raw[base + 2], raw[base + 3]),
                new Vec3f(raw[base + 4], raw[base + 5], raw[base + 6]),
                raw[base + 7]
            );
        }
        return hits;
    }

    // Check if two entities are overlapping
    public static function isOverlapping(int entityA, int entityB): bool {
        return _native_physics_isOverlapping(entityA, entityB);
    }

    // ============================================
    // World Settings
    // ============================================

    // Get current gravity
    public static function getGravity(): Vec3f {
        float[] v = _native_physics_getGravity();
        return new Vec3f(v[0], v[1], v[2]);
    }

    // Set world gravity
    public static function setGravity(Vec3f gravity): void {
        _native_physics_setGravity(gravity.x, gravity.y, gravity.z);
    }
}
