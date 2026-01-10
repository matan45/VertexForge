// ICollisionListener - Interface for receiving collision events
// Implement this interface in @Script classes to receive collision callbacks
//
// Usage:
//   @Script
//   public class PlayerController implements ICollisionListener {
//       @Override
//       public function onCollisionEnter(int otherEntityId): void {
//           Log::info("Collided with entity: " + otherEntityId);
//       }
//
//       @Override
//       public function onCollisionExit(int otherEntityId): void {
//           Log::info("Stopped colliding with entity: " + otherEntityId);
//       }
//   }

interface ICollisionListener {
    // Called when this entity starts colliding with another solid entity
    function onCollisionEnter(int otherEntityId): void;

    // Called when this entity stops colliding with another solid entity
    function onCollisionExit(int otherEntityId): void;
}
