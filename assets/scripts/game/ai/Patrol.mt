// Patrol - BT ScriptTask: walk back and forth between two blackboard points.
// Runs forever ("running") — wrap it in a guarded Selector branch so combat
// or orders can preempt it via observer aborts.
//
// Blackboard in: patrolA (vec3), patrolB (vec3)
// Returns: "running" always
// onAbort (observer aborts): stops the nav agent and re-issues on resume

import * from "engine/Entity.mt";
import * from "engine/Navmesh.mt";
import * from "engine/Blackboard.mt";
import * from "math/Vec3f.mt";

@Script
public class Patrol {
    private int selfId;
    private bool movingToB;
    private bool destinationIssued;

    public constructor() {
        this.selfId = -1;
        this.movingToB = false;
        this.destinationIssued = false;
    }

    public function onStart(): void {
        this.selfId = Entity::self();
    }

    public function tick(float deltaTime): string {
        float[] target = Blackboard::getVec3(this.selfId, "patrolA");
        if (this.movingToB) {
            target = Blackboard::getVec3(this.selfId, "patrolB");
        }

        Vec3f selfPos = Entity::getPosition(this.selfId);
        float dx = target[0] - selfPos.x;
        float dz = target[2] - selfPos.z;
        float distSq = dx * dx + dz * dz;

        if (distSq < 2.25) {
            this.movingToB = !this.movingToB;
            this.destinationIssued = false;
            return "running";
        }

        if (!this.destinationIssued) {
            Navmesh::setDestination(this.selfId, new Vec3f(target[0], target[1], target[2]));
            this.destinationIssued = true;
        }

        return "running";
    }

    public function onAbort(): void {
        Navmesh::stopAgent(this.selfId);
        this.destinationIssued = false;
    }

    public function onDestroy(): void {
    }
}
