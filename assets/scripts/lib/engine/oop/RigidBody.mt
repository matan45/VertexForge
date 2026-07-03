// RigidBody - Typed wrapper over the entity's physics rigid body.
// Stateless view forwarding 1:1 to the Physics static facade.
//
// Usage:
//   RigidBody rb = this.gameObject().rigidBody();
//   if (rb.exists()) { rb.applyImpulse(dir.multiply(30.0)); }

import * from "../../math/Vec3f.mt";
import * from "../../math/Quaternion.mt";
import * from "../Physics.mt";
import * from "Component.mt";

public class RigidBody extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Physics::hasRigidBody(this.entityId);
    }

    // --- body configuration ---

    // Physics.BODY_* constant
    public function bodyType(): int {
        return Physics::getBodyType(this.entityId);
    }

    public function setBodyType(int type): void {
        Physics::setBodyType(this.entityId, type);
    }

    public function mass(): float {
        return Physics::getMass(this.entityId);
    }

    public function setMass(float mass): void {
        Physics::setMass(this.entityId, mass);
    }

    public function linearDamping(): float {
        return Physics::getLinearDamping(this.entityId);
    }

    public function setLinearDamping(float damping): void {
        Physics::setLinearDamping(this.entityId, damping);
    }

    public function angularDamping(): float {
        return Physics::getAngularDamping(this.entityId);
    }

    public function setAngularDamping(float damping): void {
        Physics::setAngularDamping(this.entityId, damping);
    }

    // --- velocities ---

    public function linearVelocity(): Vec3f {
        return Physics::getLinearVelocity(this.entityId);
    }

    public function setLinearVelocity(Vec3f velocity): void {
        Physics::setLinearVelocity(this.entityId, velocity);
    }

    public function angularVelocity(): Vec3f {
        return Physics::getAngularVelocity(this.entityId);
    }

    public function setAngularVelocity(Vec3f velocity): void {
        Physics::setAngularVelocity(this.entityId, velocity);
    }

    // --- forces ---

    public function applyForce(Vec3f force): void {
        Physics::applyForce(this.entityId, force);
    }

    public function applyForceAtPosition(Vec3f force, Vec3f position): void {
        Physics::applyForceAtPosition(this.entityId, force, position);
    }

    public function applyImpulse(Vec3f impulse): void {
        Physics::applyImpulse(this.entityId, impulse);
    }

    public function applyTorque(Vec3f torque): void {
        Physics::applyTorque(this.entityId, torque);
    }

    // --- simulated transform (physics world, not the scene transform) ---

    public function position(): Vec3f {
        return Physics::getPosition(this.entityId);
    }

    public function setPosition(Vec3f position): void {
        Physics::setPosition(this.entityId, position);
    }

    public function rotation(): Quaternion {
        return Physics::getRotation(this.entityId);
    }

    public function setRotation(Quaternion rotation): void {
        Physics::setRotation(this.entityId, rotation);
    }

    // --- body lifecycle ---

    public function createBody(): bool {
        return Physics::createBody(this.entityId);
    }

    public function rebuildBody(): bool {
        return Physics::rebuildBody(this.entityId);
    }

    public function destroyBody(): bool {
        return Physics::destroyBody(this.entityId);
    }
}
