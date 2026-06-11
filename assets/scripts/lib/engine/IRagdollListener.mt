// IRagdollListener - Interface for receiving ragdoll state events
// Implement this interface in @Script classes attached to an entity with a
// PhysicsAnimationComponent to receive ragdoll lifecycle callbacks.
//
// Usage:
//   @Script
//   public class EnemyController implements IRagdollListener {
//       @Override
//       public function onRagdollActivated(): void {
//           Log::info("Ragdoll activated");
//       }
//
//       @Override
//       public function onRagdollDeactivated(): void {
//           Log::info("Back to animation");
//       }
//
//       @Override
//       public function onRagdollSettled(): void {
//           // Body came to rest - good moment to start a get-up animation:
//           // PhysicsAnimation::setMode(Entity::self(), PhysicsAnimation::MODE_ANIMATED);
//       }
//   }

interface IRagdollListener {
    // Called when the entity enters Ragdoll or PoweredRagdoll mode
    function onRagdollActivated(): void;

    // Called when the entity leaves ragdoll modes (back to Animated/Kinematic)
    function onRagdollDeactivated(): void;

    // Called once when all ragdoll bodies have been near-still for the
    // configured settle window (resets if the ragdoll is disturbed again)
    function onRagdollSettled(): void;
}
