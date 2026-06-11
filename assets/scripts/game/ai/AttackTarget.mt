// AttackTarget - BT ScriptTask: chase the blackboard enemy and land hits.
// Placeholder damage (log only) until the RTS demo combat model lands;
// succeeds when the target despawns or deactivates.
//
// Blackboard in: enemyId (int), attackRange (float, default 2.5)
// Blackboard out: enemyPos (vec3, kept fresh while chasing)
// Returns: "running" while chasing/attacking, "success" when the target is gone,
//          "failure" when there is no valid target to begin with
// onAbort (observer aborts): stops the nav agent mid-chase

import * from "engine/Log.mt";
import * from "engine/Entity.mt";
import * from "engine/Navmesh.mt";
import * from "engine/Blackboard.mt";
import * from "math/Vec3f.mt";

@Script
public class AttackTarget {
    private int selfId;
    private float attackTimer;
    private bool engaged;

    public constructor() {
        this.selfId = -1;
        this.attackTimer = 0.0;
        this.engaged = false;
    }

    public function onStart(): void {
        this.selfId = Entity::self();
    }

    public function tick(float deltaTime): string {
        int enemyId = Blackboard::getInt(this.selfId, "enemyId");
        if (enemyId < 0) {
            return "failure";
        }

        if (!Entity::isValid(enemyId) || !Entity::isActive(enemyId)) {
            Navmesh::stopAgent(this.selfId);
            this.engaged = false;
            return "success";
        }

        float attackRange = Blackboard::getFloat(this.selfId, "attackRange");
        if (attackRange <= 0.0) {
            attackRange = 2.5;
        }

        Vec3f selfPos = Entity::getPosition(this.selfId);
        Vec3f enemyPos = Entity::getPosition(enemyId);
        Blackboard::setVec3(this.selfId, "enemyPos", enemyPos.x, enemyPos.y, enemyPos.z);

        float dx = enemyPos.x - selfPos.x;
        float dy = enemyPos.y - selfPos.y;
        float dz = enemyPos.z - selfPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq > attackRange * attackRange) {
            Navmesh::setDestination(this.selfId, enemyPos);
            this.engaged = false;
            return "running";
        }

        if (!this.engaged) {
            Navmesh::stopAgent(this.selfId);
            this.engaged = true;
            this.attackTimer = 0.0;
        }

        this.attackTimer = this.attackTimer - deltaTime;
        if (this.attackTimer <= 0.0) {
            this.attackTimer = 1.0;
            // Placeholder hit: swap for the RTS damage native once combat lands
            Log::info("[AI] " + Entity::getName(this.selfId) + " attacks " + Entity::getName(enemyId));
        }

        return "running";
    }

    public function onAbort(): void {
        Navmesh::stopAgent(this.selfId);
        this.engaged = false;
    }

    public function onDestroy(): void {
    }
}
