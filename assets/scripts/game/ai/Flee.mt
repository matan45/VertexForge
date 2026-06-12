// Flee - BT ScriptTask: run directly away from the blackboard enemy position.
// Succeeds once the entity is at least <fleeDistance> away from the threat.
//
// Blackboard in: enemyPos (vec3), fleeDistance (float, default 15)
// Returns: "running" while fleeing, "success" once clear
// onAbort (observer aborts): stops the nav agent

import * from "engine/Entity.mt";
import * from "engine/Navmesh.mt";
import * from "engine/Blackboard.mt";
import * from "math/Vec3f.mt";

@Script
public class Flee {
    private int selfId;
    private float reissueTimer;

    public constructor() {
        this.selfId = -1;
        this.reissueTimer = 0.0;
    }

    public function onStart(): void {
        this.selfId = Entity::self();
    }

    public function tick(float deltaTime): string {
        float fleeDistance = Blackboard::getFloat(this.selfId, "fleeDistance");
        if (fleeDistance <= 0.0) {
            fleeDistance = 15.0;
        }

        float[] enemyPos = Blackboard::getVec3(this.selfId, "enemyPos");
        Vec3f selfPos = Entity::getPosition(this.selfId);

        float dx = selfPos.x - enemyPos[0];
        float dz = selfPos.z - enemyPos[2];
        float distSq = dx * dx + dz * dz;

        if (distSq >= fleeDistance * fleeDistance) {
            Navmesh::stopAgent(this.selfId);
            return "success";
        }

        // Re-issue the escape destination periodically since the threat moves
        this.reissueTimer = this.reissueTimer - deltaTime;
        if (this.reissueTimer <= 0.0) {
            this.reissueTimer = 0.5;

            float len = sqrt(distSq);
            float awayX = 1.0;
            float awayZ = 0.0;
            if (len > 0.01) {
                awayX = dx / len;
                awayZ = dz / len;
            }

            Vec3f destination = new Vec3f(selfPos.x + awayX * fleeDistance,
                                          selfPos.y,
                                          selfPos.z + awayZ * fleeDistance);
            Navmesh::setDestination(this.selfId, Navmesh::getClosestPoint(destination));
        }

        return "running";
    }

    public function onAbort(): void {
        Navmesh::stopAgent(this.selfId);
        this.reissueTimer = 0.0;
    }

    public function onDestroy(): void {
    }
}
