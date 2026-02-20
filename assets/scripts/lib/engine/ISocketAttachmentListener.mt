// ISocketAttachmentListener - Interface for receiving socket attachment events
// Implement this interface in @Script classes to be notified when entities
// are attached to or detached from sockets on this entity (or on a parent entity)
//
// Callbacks fire on BOTH the parent entity (the one with the skeleton/sockets)
// and the child entity (the one being attached)
//
// Usage:
//   @Script
//   public class WeaponHolder implements ISocketAttachmentListener {
//       @Override
//       public function onSocketAttached(int parentEntityId, string socketName, int childEntityId): void {
//           Log::info("Entity " + childEntityId + " attached to socket " + socketName);
//       }
//
//       @Override
//       public function onSocketDetached(int parentEntityId, string socketName, int childEntityId): void {
//           Log::info("Entity " + childEntityId + " detached from socket " + socketName);
//       }
//   }

interface ISocketAttachmentListener {
    // Called when an entity is attached to a socket
    function onSocketAttached(int parentEntityId, string socketName, int childEntityId): void;

    // Called when an entity is detached from a socket
    function onSocketDetached(int parentEntityId, string socketName, int childEntityId): void;
}
