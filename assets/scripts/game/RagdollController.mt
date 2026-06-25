// RagdollController - VK-1440 ragdoll validation demo
// Attach to an entity that has a PhysicsAnimationComponent (bone body mappings).
// Implements IRagdollListener to receive ragdoll lifecycle callbacks and uses
// the PhysicsAnimation API to drive the entity through its physics modes.
//
// Key bindings (GLFW key codes, rising-edge / one-shot):
//   R - knockback death: activate ragdoll with a backward impulse
//   P - powered ragdoll: motors track the playing animation
//   T - recover: return to animation-driven mode
//   H - hit reaction: flinch impulse on the "Chest" bone

import * from "../lib/engine/Log.mt";
import * from "../lib/engine/Entity.mt";
import * from "../lib/engine/Input.mt";
import * from "../lib/engine/PhysicsAnimation.mt";
import * from "../lib/engine/IRagdollListener.mt";
import * from "../lib/math/Vec3f.mt";

@Script
public class RagdollController implements IRagdollListener {
    // ============================================
    // Key codes (GLFW): R=82, P=80, H=72, T=84
    // ============================================
    private final int KEY_R = 82;
    private final int KEY_P = 80;
    private final int KEY_H = 72;
    private final int KEY_T = 84;

    // Cached entity id this script is attached to
    private int selfId = -1;

    // Listener-tracked ragdoll state (mirrors callbacks)
    private bool isRagdoll = false;

    // Guard so the auto get-up on settle only fires once per ragdoll
    private bool settleHandled = false;

    // Previous-frame key state for rising-edge (one-shot) detection
    private bool rDownLast = false;
    private bool pDownLast = false;
    private bool hDownLast = false;
    private bool tDownLast = false;

    public constructor() {
        // Fields keep their declared defaults
    }

    public function onStart(): void {
        this.selfId = Entity::self();
        Log::info("RagdollController started on entity: " + Entity::getName(this.selfId));

        if (!PhysicsAnimation::hasPhysicsAnimation(this.selfId)) {
            Log::warn("RagdollController: entity has no PhysicsAnimationComponent - ragdoll controls will do nothing.");
        }
    }

    public function onUpdate(float deltaTime): void {
        // Sample current key states once
        bool rDown = Input::isKeyDown(this.KEY_R);
        bool pDown = Input::isKeyDown(this.KEY_P);
        bool hDown = Input::isKeyDown(this.KEY_H);
        bool tDown = Input::isKeyDown(this.KEY_T);

        // R - knockback death (only if not already in a ragdoll mode)
        if (rDown && !this.rDownLast) {
            int mode = PhysicsAnimation::getMode(this.selfId);
            if (mode != PhysicsAnimation::MODE_RAGDOLL && mode != PhysicsAnimation::MODE_POWERED_RAGDOLL) {
                PhysicsAnimation::activateRagdollWithImpulse(this.selfId, new Vec3f(0.0, 3.0, -4.0));
                Log::info("RagdollController: R - activated ragdoll with knockback impulse.");
            } else {
                Log::info("RagdollController: R ignored - already in a ragdoll mode.");
            }
        }

        // P - powered ragdoll (physical animation tracking)
        if (pDown && !this.pDownLast) {
            PhysicsAnimation::activatePoweredRagdoll(this.selfId);
            Log::info("RagdollController: P - activated powered ragdoll (animation tracking).");
        }

        // T - recover to animation-driven mode
        if (tDown && !this.tDownLast) {
            PhysicsAnimation::setMode(this.selfId, PhysicsAnimation::MODE_ANIMATED);
            Log::info("RagdollController: T - recovered to animated mode.");
        }

        // H - hit reaction flinch on the chest bone
        if (hDown && !this.hDownLast) {
            PhysicsAnimation::hitReaction(this.selfId, "Chest", new Vec3f(40.0, 15.0, 0.0));
            Log::info("RagdollController: H - hit reaction on Chest.");
        }

        // Store this frame's key states for next frame's rising-edge check
        this.rDownLast = rDown;
        this.pDownLast = pDown;
        this.hDownLast = hDown;
        this.tDownLast = tDown;
    }

    public function onDestroy(): void {
        Log::info("RagdollController destroyed.");
    }

    // ============================================
    // IRagdollListener callbacks
    // ============================================

    @Override
    public function onRagdollActivated(): void {
        this.isRagdoll = true;
        this.settleHandled = false;
        Log::info("RagdollController: ragdoll activated.");
    }

    @Override
    public function onRagdollDeactivated(): void {
        this.isRagdoll = false;
        Log::info("RagdollController: ragdoll deactivated (back to animation).");
    }

    @Override
    public function onRagdollSettled(): void {
        // The body has come to rest - this is the cue to start a get-up animation.
        // As a demo, switch back to animation-driven mode once per ragdoll.
        Log::info("RagdollController: ragdoll settled - body at rest.");
        if (!this.settleHandled) {
            this.settleHandled = true;
            PhysicsAnimation::setMode(this.selfId, PhysicsAnimation::MODE_ANIMATED);
            Log::info("RagdollController: auto get-up - returning to animated mode.");
        }
    }
}
