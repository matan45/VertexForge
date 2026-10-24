// WaterSplash - Example of reacting to water through IWaterListener (VK-1658).
//
// Attach it to an entity with a dynamic RigidBody and a Collider. When the
// entity drops into water (a lake, a pool or the visible ocean) faster than
// 1 m/s, a splash plays on the surface above it. verticalSpeed is the vertical
// velocity at impact: negative while falling. The path is relative to the
// project folder: copy the engine's assets/vfx/library into the project as
// assets/vfx/library (vfx_list_assets lists what is there).

import * from "../../lib/engine/oop/Behaviour.mt";
import * from "../../lib/engine/IWaterListener.mt";
import * from "../../lib/engine/Ocean.mt";
import * from "../../lib/engine/VFX.mt";
import * from "../../lib/math/Vec3f.mt";

@Script
public class WaterSplash extends Behaviour implements IWaterListener {
    private string effectPath = "assets/vfx/library/rain_splash.vfVFX";

    public constructor() : super() {
    }

    @Override
    public function onWaterEnter(float verticalSpeed): void {
        if (verticalSpeed < -1.0) {
            Vec3f at = this.transform().worldPosition();
            float surface = Ocean::getOceanHeightAt(at.x, at.z);
            int instanceId = VFX::spawnAt(this.effectPath, at.x, surface, at.z);
            if (instanceId == 0) {
                this.logWarn("no splash spawned from " + this.effectPath);
            }
        }
    }

    @Override
    public function onWaterExit(): void {
    }

    @Override
    public function onEntityWaterEnter(int entityId, float x, float y, float z,
                                       float verticalSpeed): void {
    }

    @Override
    public function onEntityWaterExit(int entityId): void {
    }
}
