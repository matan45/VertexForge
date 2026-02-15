// IUIDropdownListener - Interface for receiving UI dropdown events
// Implement this interface in @Script classes to receive dropdown callbacks
// Unlike collision callbacks, dropdown events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUIDropdownListener;
//
//   @Script
//   public class MenuController implements IUIDropdownListener {
//       @Override
//       public function onDropdownOpened(int entityId, String entityName): void {
//           Log::info("Dropdown opened: " + entityName);
//       }
//
//       @Override
//       public function onDropdownClosed(int entityId, String entityName): void { }
//
//       @Override
//       public function onDropdownSelectionChanged(int entityId, String entityName, int previousIndex, int newIndex): void {
//           Log::info("Selection changed from " + previousIndex + " to " + newIndex);
//       }
//   }

interface IUIDropdownListener {
    // Called when a dropdown is opened
    function onDropdownOpened(int entityId, String entityName): void;

    // Called when a dropdown is closed
    function onDropdownClosed(int entityId, String entityName): void;

    // Called when the selected option changes
    function onDropdownSelectionChanged(int entityId, String entityName, int previousIndex, int newIndex): void;
}
