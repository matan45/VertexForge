// IWaterListener - Interface for receiving water enter/exit events
// Implement this interface in @Script classes to receive water callbacks.
//
// "Water" is any water that floats things: a lake or pool (water body) whose
// physics is on, or the visible ocean while its physics is on. Only dynamic
// rigid bodies (a RigidBody that is not static) are tracked, by the physics
// step, so the callbacks run only in Play mode.
//
// onWaterEnter/onWaterExit fire only when THIS script's entity transitions.
// onEntityWaterEnter/onEntityWaterExit fire for EVERY entity transition, so a
// manager script (e.g. an RTS effects controller) can spawn splashes for any unit.
//
// verticalSpeed is the entity's vertical velocity at impact in m/s: NEGATIVE
// while it falls in, so test a drop with verticalSpeed < -1.0.
//
// Usage:
//   @Script
//   public class SplashController extends Behaviour implements IWaterListener {
//       public constructor() : super() {
//       }
//
//       @Override
//       public function onWaterEnter(float verticalSpeed): void {
//           Log::info("I hit the water at " + parsePrimitive(verticalSpeed) + " m/s");
//       }
//
//       @Override
//       public function onWaterExit(): void {
//       }
//
//       @Override
//       public function onEntityWaterEnter(int entityId, float x, float y, float z,
//                                          float verticalSpeed): void {
//           VFX::spawnAt("assets/vfx/library/rain_splash.vfVFX", x, y, z);
//       }
//
//       @Override
//       public function onEntityWaterExit(int entityId): void {
//       }
//   }
// The effect path is relative to the project folder: copy the engine's
// assets/vfx/library into the project as assets/vfx/library.
// game/examples/WaterSplash.mt is a complete example.

interface IWaterListener {
    // Called when this entity enters water (verticalSpeed = vertical velocity at impact, m/s,
    // negative while falling in)
    function onWaterEnter(float verticalSpeed): void;

    // Called when this entity leaves the water
    function onWaterExit(): void;

    // Called when ANY tracked entity enters water (x, y, z = its world position at that physics
    // step, not the water surface)
    function onEntityWaterEnter(int entityId, float x, float y, float z, float verticalSpeed): void;

    // Called when ANY tracked entity leaves the water
    function onEntityWaterExit(int entityId): void;
}
