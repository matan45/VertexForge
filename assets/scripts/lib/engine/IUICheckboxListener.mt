// IUICheckboxListener - Interface for receiving UI checkbox events
// Implement this interface in @Script classes to receive checkbox callbacks
// Unlike collision callbacks, checkbox events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUICheckboxListener;
//
//   @Script
//   public class SettingsController implements IUICheckboxListener {
//       @Override
//       public function onCheckboxToggled(int entityId, string entityName, bool newState, bool previousState): void {
//           Log::info("Checkbox toggled: " + entityName + " -> " + newState);
//       }
//
//       @Override
//       public function onCheckboxHoverEnter(int entityId, string entityName): void { }
//       @Override
//       public function onCheckboxHoverExit(int entityId, string entityName): void { }
//   }

interface IUICheckboxListener {
    // Called when a checkbox is toggled (checked/unchecked)
    function onCheckboxToggled(int entityId, string entityName, bool newState, bool previousState): void;

    // Called when the mouse enters a checkbox's area
    function onCheckboxHoverEnter(int entityId, string entityName): void;

    // Called when the mouse leaves a checkbox's area
    function onCheckboxHoverExit(int entityId, string entityName): void;
}
