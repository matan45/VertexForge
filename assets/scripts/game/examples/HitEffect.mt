// HitEffect - Example of spawning a VFX one-shot on collision (VK-1657).
//
// Collision callbacks carry only the other entity's id, not the contact
// point, so the effect plays at this entity's world position. The path is
// relative to the project folder: copy the engine's assets/vfx/library into
// the project as assets/vfx/library (vfx_list_assets lists what is there).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/ICollisionListener.mt";
import * from "../../lib/engine/VFX.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class HitEffect extends Behaviour implements ICollisionListener {
    private string effectPath = "assets/vfx/library/sparks.vfVFX";

    public constructor() : super() {
    }

    @Override
    public function onCollisionEnter(int otherEntityId): void {
        Vec3f at = this.transform().worldPosition();
        int instanceId = VFX::spawnAt(this.effectPath, at.x, at.y, at.z);
        if (instanceId == 0) {
            this.logWarn("no effect spawned from " + this.effectPath);
        }
    }

    @Override
    public function onCollisionExit(int otherEntityId): void {
    }
}
