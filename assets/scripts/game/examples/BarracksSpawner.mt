// BarracksSpawner - Example of typed prefab references (VK-1458 OOP style).
//
// PrefabRef holds the path once at the declaration site; spawning returns a
// GameObject (null on failure) instead of a raw id.

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/oop/GameObject.mt";
import * from "../../lib/engine/oop/PrefabRef.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class BarracksSpawner extends Behaviour {
    private PrefabRef soldierPrefab = new PrefabRef("assets/units/soldier.vfPrefab");
    private float spawnInterval = 10.0;
    private float elapsed = 0.0;

    public constructor() : super() {
    }

    @Override
    public function onUpdate(float deltaTime): void {
        this.elapsed = this.elapsed + deltaTime;
        if (this.elapsed < this.spawnInterval) {
            return;
        }
        this.elapsed = 0.0;

        // Spawn 3 units in front of the barracks
        Vec3f spawnPoint = this.transform().worldPosition()
            .add(this.transform().forward().multiply(3.0));

        GameObject? soldier = this.soldierPrefab.instantiateAt(spawnPoint);
        if (soldier == null) {
            this.logWarn("soldier prefab failed to instantiate");
            return;
        }

        soldier.setName("Soldier");
        soldier.navAgent().setDestination(
            spawnPoint.add(this.transform().forward().multiply(6.0)));
    }
}
