// AnimatorLocomotion - Example of driving an Animator parameter from a script.
//
// Expects an animator with a Float parameter "speed" and two states: Idle
// (the default state) and Run, with Idle -> Run when speed > 0.1 and
// Run -> Idle when speed <= 0.1. WASD moves the entity like PlayerMovement;
// "speed" is the move speed while a key is held, else 0.0.
//
// Attach it to the character's root entity. An imported character is often
// several skinned mesh entities, each with its own animator, so the value is
// set on this entity and on every direct child that has an animator. It is
// set every frame: saving the .vfAnimator re-creates its animators with the
// default parameter values.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Animator.mt";
import * from "../../lib/engine/Entity.mt";
import * from "../../lib/engine/Input.mt";
import * from "../../lib/engine/Key.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class AnimatorLocomotion extends Behaviour {
    private float moveSpeed = 3.0;

    public constructor() : super() {
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

        float speed = 0.0;
        if (direction.lengthSquared() > 0.0) {
            this.transform().translate(direction.normalize().multiply(this.moveSpeed * deltaTime));
            speed = this.moveSpeed;
        }

        int root = this.entityId();
        Animator::setFloat(root, "speed", speed);
        int[] children = Entity::getChildren(root);
        for (int i = 0; i < children.length; i = i + 1) {
            int child = children[i];
            if (Animator::hasAnimator(child)) {
                Animator::setFloat(child, "speed", speed);
            }
        }
    }
}
