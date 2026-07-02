// Transform - Object wrapper over an entity's transform (VK-1458 OOP layer).
//
// Method-based by design (mType has no property getters — a `.position`
// field would be a stale snapshot). Names are explicit about LOCAL vs WORLD:
// the engine's cheap transform natives are local-space, and for parented
// entities the two differ, so there is deliberately no bare `position()`.
//
// Conventions (match the engine): Euler angles in DEGREES, +Z is forward,
// yaw is rotation.y (see BehaviorTreeAdapter's forward derivation).
//
// Usage:
//   Transform t = this.transform();
//   t.translate(t.forward().multiply(speed * dt));
//   t.lookAt(target.transform().worldPosition());

import * from "../../math/Vec3f.mt";
import * from "../../math/Quaternion.mt";
import * from "../Entity.mt";

public class Transform {
    public final int entityId;

    public constructor(int entityId) {
        this.entityId = entityId;
    }

    // ============================================
    // Local space (the entity's own TransformComponent)
    // ============================================

    public function localPosition(): Vec3f {
        return Entity::getPosition(this.entityId);
    }

    public function setLocalPosition(Vec3f position): void {
        Entity::setPosition(this.entityId, position);
    }

    // Euler angles in degrees
    public function localEulerAngles(): Vec3f {
        return Entity::getRotation(this.entityId);
    }

    public function setLocalEulerAngles(Vec3f eulerDegrees): void {
        Entity::setRotation(this.entityId, eulerDegrees);
    }

    public function localRotation(): Quaternion {
        return Transform::quaternionFromEulerDegrees(this.localEulerAngles());
    }

    public function setLocalRotation(Quaternion rotation): void {
        Vec3f radians = rotation.toEulerAngles();
        this.setLocalEulerAngles(new Vec3f(
            radians.x * Transform::RAD_TO_DEG,
            radians.y * Transform::RAD_TO_DEG,
            radians.z * Transform::RAD_TO_DEG));
    }

    public function localScale(): Vec3f {
        return Entity::getScale(this.entityId);
    }

    public function setLocalScale(Vec3f scale): void {
        Entity::setScale(this.entityId, scale);
    }

    public function setUniformScale(float scale): void {
        Entity::setUniformScale(this.entityId, scale);
    }

    // ============================================
    // Deltas
    // ============================================

    // Move by a local-space offset
    public function translate(Vec3f delta): void {
        this.setLocalPosition(this.localPosition().add(delta));
    }

    // Rotate by additional Euler degrees
    public function rotate(Vec3f eulerDeltaDegrees): void {
        this.setLocalEulerAngles(this.localEulerAngles().add(eulerDeltaDegrees));
    }

    // ============================================
    // World space (scene-graph resolved; equals local when unparented)
    // ============================================

    public function worldPosition(): Vec3f {
        return Entity::getWorldPosition(this.entityId);
    }

    // Euler angles in degrees, decomposed from the world matrix
    public function worldEulerAngles(): Vec3f {
        return Entity::getWorldRotation(this.entityId);
    }

    public function worldRotation(): Quaternion {
        return Transform::quaternionFromEulerDegrees(this.worldEulerAngles());
    }

    // ============================================
    // Directions (world space, +Z forward convention)
    // ============================================

    public function forward(): Vec3f {
        return this.worldRotation().rotate(Vec3f::unitZ());
    }

    public function right(): Vec3f {
        return this.worldRotation().rotate(Vec3f::unitX());
    }

    public function up(): Vec3f {
        return this.worldRotation().rotate(Vec3f::unitY());
    }

    // Face a world-space point. Writes LOCAL rotation, so this is exact for
    // unparented entities and an approximation under a rotated parent (no
    // set-world-rotation native yet).
    public function lookAt(Vec3f worldTarget): void {
        Vec3f toTarget = worldTarget.subtract(this.worldPosition());
        if (toTarget.lengthSquared() < 0.000001) {
            return;
        }
        Quaternion q = Quaternion::lookRotation(toTarget, Vec3f::unitY());
        this.setLocalRotation(q);
    }

    // ============================================
    // Hierarchy
    // ============================================

    public function parent(): Transform? {
        int parentId = Entity::getParent(this.entityId);
        if (parentId < 0) {
            return null;
        }
        return new Transform(parentId);
    }

    // ============================================
    // Conversion helpers
    // ============================================

    private static final float DEG_TO_RAD = 0.017453292519943295;
    private static final float RAD_TO_DEG = 57.29577951308232;

    public static function quaternionFromEulerDegrees(Vec3f eulerDegrees): Quaternion {
        return Quaternion::fromEulerAngles(
            eulerDegrees.x * Transform::DEG_TO_RAD,
            eulerDegrees.y * Transform::DEG_TO_RAD,
            eulerDegrees.z * Transform::DEG_TO_RAD);
    }
}
