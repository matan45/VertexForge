// Collider - Typed wrapper over the entity's physics collider.
// Stateless view forwarding 1:1 to the Physics static facade.

import * from "../../math/Vec3f.mt";
import * from "../Physics.mt";
import * from "Component.mt";

public class Collider extends Component {
    public constructor(int entityId) : super(entityId) {
    }

    @Override
    public function exists(): bool {
        return Physics::hasCollider(this.entityId);
    }

    // Physics.SHAPE_* constant
    public function shape(): int {
        return Physics::getColliderShape(this.entityId);
    }

    public function size(): Vec3f {
        return Physics::getColliderSize(this.entityId);
    }

    public function setSize(Vec3f size): void {
        Physics::setColliderSize(this.entityId, size);
    }

    public function height(): float {
        return Physics::getColliderHeight(this.entityId);
    }

    public function setHeight(float height): void {
        Physics::setColliderHeight(this.entityId, height);
    }

    public function offset(): Vec3f {
        return Physics::getColliderOffset(this.entityId);
    }

    public function isTrigger(): bool {
        return Physics::isTrigger(this.entityId);
    }

    public function setTrigger(bool isTrigger): void {
        Physics::setTrigger(this.entityId, isTrigger);
    }

    public function collisionLayer(): int {
        return Physics::getCollisionLayer(this.entityId);
    }

    public function setCollisionLayer(int layer): void {
        Physics::setCollisionLayer(this.entityId, layer);
    }

    public function friction(): float {
        return Physics::getFriction(this.entityId);
    }

    public function setFriction(float friction): void {
        Physics::setFriction(this.entityId, friction);
    }

    public function restitution(): float {
        return Physics::getRestitution(this.entityId);
    }

    public function setRestitution(float restitution): void {
        Physics::setRestitution(this.entityId, restitution);
    }
}
