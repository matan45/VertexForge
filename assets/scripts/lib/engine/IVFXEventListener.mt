// IVFXEventListener - Interface for receiving VFX particle events
// Implement this interface in @Script classes to receive callbacks when
// particle lifecycle events fire (spawn, death, collision, lifetime threshold)
//
// Events are emitted from the GPU particle simulation and dispatched
// one frame later via readback. Each event carries position and velocity
// of the particle that triggered it.
//
// Event types:
//   0 = OnSpawn     - particle was just spawned
//   1 = OnDeath     - particle reached end of lifetime
//   2 = OnCollision - particle collided with a scene collider or terrain
//   3 = OnLifetimeThreshold - particle crossed the configured lifetime ratio
//
// Usage:
//   import * from "Vec3f.mt";
//
//   @Script
//   public class VFXReactor implements IVFXEventListener {
//       @Override
//       public function onVFXParticleEvent(int eventType, Vec3f position, Vec3f velocity, int entityId): void {
//           if (eventType == 1) {
//               Log::info("Particle died at: " + position.toString());
//           }
//       }
//   }

import * from "Vec3f.mt";

interface IVFXEventListener {
    // Called when a VFX particle event fires
    // eventType: 0=OnSpawn, 1=OnDeath, 2=OnCollision, 3=OnLifetimeThreshold
    // position: world-space position of the particle
    // velocity: velocity of the particle at event time
    // entityId: the entity that owns the VFX emitter
    function onVFXParticleEvent(int eventType, Vec3f position, Vec3f velocity, int entityId): void;
}
