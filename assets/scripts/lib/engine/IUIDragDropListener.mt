// IUIDragDropListener - Interface for receiving UI drag and drop events
// Implement this interface in @Script classes to receive drag/drop callbacks
// Unlike collision callbacks, drag/drop events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUIDragDropListener;
//
//   @Script
//   public class InventoryManager implements IUIDragDropListener {
//       @Override
//       public function onDragStart(int entityId, string entityName, string dragTag): void {
//           Log::info("Started dragging: " + entityName);
//       }
//
//       @Override
//       public function onDragEnd(int entityId, string entityName, boolean wasDropped): void {
//           Log::info("Drag ended: " + entityName + " dropped=" + wasDropped);
//       }
//
//       @Override
//       public function onDrop(int sourceEntityId, String sourceEntityName, int targetEntityId, String targetEntityName, string dragTag): void {
//           Log::info("Dropped " + sourceEntityName + " onto " + targetEntityName);
//       }
//   }

interface IUIDragDropListener {
    // Called when the user starts dragging an entity with UIDraggableComponent
    function onDragStart(int entityId, string entityName, string dragTag): void;

    // Called when the user releases the mouse after dragging
    // wasDropped is true if the entity was dropped on a valid UIDropTargetComponent
    function onDragEnd(int entityId, string entityName, boolean wasDropped): void;

    // Called when a dragged entity is dropped on a valid drop target
    // sourceEntityId/Name = the dragged entity, targetEntityId/Name = the drop target
    function onDrop(int sourceEntityId, String sourceEntityName, int targetEntityId, String targetEntityName, string dragTag): void;
}
