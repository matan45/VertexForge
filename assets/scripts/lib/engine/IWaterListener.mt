// IWaterListener - Interface for receiving ocean water enter/exit events
// Implement this interface in @Script classes to receive water callbacks.
//
// onWaterEnter/onWaterExit fire only when THIS script's entity transitions.
// onEntityWaterEnter/onEntityWaterExit fire for EVERY entity transition, so a
// manager script (e.g. an RTS effects controller) can spawn splashes for any unit.
//
// Usage:
//   @Script
//   public class SplashController implements IWaterListener {
//       @Override
//       public function onWaterEnter(float verticalSpeed): void {
//           Log::info("I hit the water at " + verticalSpeed + " m/s");
//       }
//
//       @Override
//       public function onWaterExit(): void {
//       }
//
//       @Override
//       public function onEntityWaterEnter(int entityId, float x, float y, float z,
//                                          float verticalSpeed): void {
//           VFX::spawnAt("splash", x, y, z);
//       }
//
//       @Override
//       public function onEntityWaterExit(int entityId): void {
//       }
//   }

interface IWaterListener {
    // Called when this entity enters the ocean (verticalSpeed = downward speed at impact, m/s)
    function onWaterEnter(float verticalSpeed): void;

    // Called when this entity leaves the ocean
    function onWaterExit(): void;

    // Called when ANY entity enters the ocean (position = entity world position)
    function onEntityWaterEnter(int entityId, float x, float y, float z, float verticalSpeed): void;

    // Called when ANY entity leaves the ocean
    function onEntityWaterExit(int entityId): void;
}
