// FindNearestEnemy - BT ScriptTask: perception tick for AI brains.
// Scans for the closest active entity named <enemyName> within <aggroRange>
// and publishes the result to the blackboard for guards and Engage subtrees.
//
// Blackboard in:  enemyName (string), aggroRange (float, default 25)
// Blackboard out: enemyVisible (bool), enemyId (int), enemyPos (vec3)
// Returns: "success" when an enemy is in range, "failure" otherwise

import * from "engine/Entity.mt";
import * from "engine/Blackboard.mt";
import * from "math/Vec3f.mt";

@Script
public class FindNearestEnemy {
    private int selfId;

    public constructor() {
        this.selfId = -1;
    }

    public function onStart(): void {
        this.selfId = Entity::self();
    }

    public function tick(float deltaTime): string {
        string enemyName = Blackboard::getString(this.selfId, "enemyName");
        if (enemyName == "") {
            return "failure";
        }

        float aggroRange = Blackboard::getFloat(this.selfId, "aggroRange");
        if (aggroRange <= 0.0) {
            aggroRange = 25.0;
        }

        Vec3f selfPos = Entity::getPosition(this.selfId);
        int[] candidates = Entity::findAll(enemyName);

        int nearestId = -1;
        float nearestDistSq = aggroRange * aggroRange;

        for (int i = 0; i < candidates.length; i = i + 1) {
            int candidateId = candidates[i];
            if (candidateId != this.selfId && Entity::isValid(candidateId) && Entity::isActive(candidateId)) {
                Vec3f pos = Entity::getPosition(candidateId);
                float dx = pos.x - selfPos.x;
                float dy = pos.y - selfPos.y;
                float dz = pos.z - selfPos.z;
                float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq < nearestDistSq) {
                    nearestDistSq = distSq;
                    nearestId = candidateId;
                }
            }
        }

        if (nearestId < 0) {
            Blackboard::setBool(this.selfId, "enemyVisible", false);
            return "failure";
        }

        Vec3f enemyPos = Entity::getPosition(nearestId);
        Blackboard::setInt(this.selfId, "enemyId", nearestId);
        Blackboard::setVec3(this.selfId, "enemyPos", enemyPos.x, enemyPos.y, enemyPos.z);
        Blackboard::setBool(this.selfId, "enemyVisible", true);
        return "success";
    }

    public function onDestroy(): void {
    }
}
