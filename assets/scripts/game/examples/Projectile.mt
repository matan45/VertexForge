// Projectile - Example of physics + listener callbacks (VK-1458 OOP style).
//
// Listener interfaces stay interfaces (the engine's dispatch gate keys on
// the implements clause); inside the callback the other entity is wrapped
// with GameObject::fromId for object-style access.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/oop/GameObject.mt";
import * from "../../lib/engine/oop/RigidBody.mt";
import * from "../../lib/engine/ICollisionListener.mt";

@Script
public class Projectile extends Behaviour implements ICollisionListener {
    private float launchSpeed = 30.0;
    private float lifetime = 5.0;
    private float age = 0.0;

    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        RigidBody body = this.gameObject().rigidBody();
        if (body.exists()) {
            body.applyImpulse(this.transform().forward().multiply(this.launchSpeed));
        } else {
            this.logWarn("Projectile has no rigid body - add one to the prefab");
        }
    }

    @Override
    public function onUpdate(float deltaTime): void {
        this.age = this.age + deltaTime;
        if (this.age > this.lifetime) {
            this.destroySelf();
        }
    }

    @Override
    public function onCollisionEnter(int otherEntityId): void {
        GameObject other = GameObject::fromId(otherEntityId);
        this.log("hit " + other.name());
        this.destroySelf();
    }

    @Override
    public function onCollisionExit(int otherEntityId): void {
    }
}
