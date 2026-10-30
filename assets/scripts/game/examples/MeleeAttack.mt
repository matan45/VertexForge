// MeleeAttack - Example of a behavior tree ScriptTask (VK-1660).
//
// A ScriptTask node runs this class through the behavior tree, not through
// onUpdate: the tree calls onStart once, then tick(deltaTime) every frame
// while the node runs ("running", "success", anything else = failure),
// onAbort() when a condition or a higher-priority branch interrupts it, and
// onEnd(success) when tick finished it. One instance per entity lives for the
// whole Play session, so per-attack state is reset in onAbort and onEnd.
//
// Blackboard: target (Entity) = who to hit, hits (Int) = hits landed so far.
// Guard the node with a BlackboardCondition on the distance to the target
// (abortMode Both): the tree stops this task when the target leaves range.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/Blackboard.mt";
import * from "../../lib/engine/Entity.mt";
import * from "../../lib/engine/Navmesh.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class MeleeAttack extends Behaviour {
    private float swingInterval = 1.0;
    private float cooldown = 0.0;
    private bool engaged = false;

    public constructor() : super() {
    }

    public function tick(float deltaTime): string {
        int selfId = this.entityId();
        int target = Blackboard::getEntity(selfId, "target");
        if (!Entity::isValid(target)) {
            return "failure";
        }
        if (!this.engaged) {
            this.engaged = true;
            this.cooldown = 0.0;
            Navmesh::stopAgent(selfId);
        }
        Vec3f selfPos = this.transform().worldPosition();
        Vec3f targetPos = Entity::getWorldPosition(target);
        this.transform().lookAt(new Vec3f(targetPos.x, selfPos.y, targetPos.z));

        this.cooldown = this.cooldown - deltaTime;
        if (this.cooldown <= 0.0) {
            this.cooldown = this.swingInterval;
            int hits = Blackboard::getInt(selfId, "hits") + 1;
            Blackboard::setInt(selfId, "hits", hits);
            this.log("hits " + Entity::getName(target) + " (" + parsePrimitive(hits) + ")");
        }
        return "running";
    }

    public function onAbort(): void {
        this.engaged = false;
    }

    public function onEnd(bool success): void {
        this.engaged = false;
    }
}
