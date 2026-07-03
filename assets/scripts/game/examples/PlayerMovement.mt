// PlayerMovement - Example of the VK-1458 OOP scripting style.
//
// Extends Behaviour: entity access via this.transform()/this.gameObject(),
// lifecycle hooks inherited from the base and overridden selectively.
// WASD moves the entity in its own facing plane (+Z forward convention).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Input.mt";
import * from "../../lib/engine/Key.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class PlayerMovement extends Behaviour {
    private float speed = 5.0;

    public constructor() : super() {
    }

    @Override
    public function onStart(): void {
        this.log("PlayerMovement ready");
    }

    @Override
    public function onUpdate(float deltaTime): void {
        Vec3f direction = Vec3f::zero();

        if (Input::isKeyDown(Key::W)) {
            direction = direction.add(this.transform().forward());
        }
        if (Input::isKeyDown(Key::S)) {
            direction = direction.subtract(this.transform().forward());
        }
        if (Input::isKeyDown(Key::D)) {
            direction = direction.add(this.transform().right());
        }
        if (Input::isKeyDown(Key::A)) {
            direction = direction.subtract(this.transform().right());
        }

        if (direction.lengthSquared() > 0.0) {
            this.transform().translate(direction.normalize().multiply(this.speed * deltaTime));
        }
    }
}
