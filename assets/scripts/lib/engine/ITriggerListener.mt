// ITriggerListener - Interface for receiving trigger events
// Implement this interface in @Script classes to receive trigger callbacks
//
// Triggers are colliders with isTrigger=true that detect overlaps without physics response
//
// Usage:
//   @Script
//   public class PickupZone implements ITriggerListener {
//       @Override
//       public function onTriggerEnter(int otherEntityId): void {
//           Log::info("Entity entered trigger: " + otherEntityId);
//       }
//
//       @Override
//       public function onTriggerExit(int otherEntityId): void {
//           Log::info("Entity exited trigger: " + otherEntityId);
//       }
//   }

interface ITriggerListener {
    // Called when another entity enters this entity's trigger collider
    function onTriggerEnter(int otherEntityId): void;

    // Called when another entity exits this entity's trigger collider
    function onTriggerExit(int otherEntityId): void;
}
