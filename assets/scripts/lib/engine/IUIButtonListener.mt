// IUIButtonListener - Interface for receiving UI button events
// Implement this interface in @Script classes to receive button callbacks
// Unlike collision callbacks, button events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUIButtonListener;
//
//   @Script
//   public class MenuController implements IUIButtonListener {
//       @Override
//       public function onButtonClicked(int buttonEntityId, String entityName): void {
//           Log::info("Button clicked: " + entityName);
//       }
//
//       @Override
//       public function onButtonPressed(int buttonEntityId, String entityName): void { }
//       @Override
//       public function onButtonReleased(int buttonEntityId, String entityName): void { }
//       @Override
//       public function onButtonHoverEnter(int buttonEntityId, String entityName): void { }
//       @Override
//       public function onButtonHoverExit(int buttonEntityId, String entityName): void { }
//   }

interface IUIButtonListener {
    // Called when a button is clicked (pressed and released while hovered)
    function onButtonClicked(int buttonEntityId, String entityName): void;

    // Called when a button is first pressed down
    function onButtonPressed(int buttonEntityId, String entityName): void;

    // Called when a button is released
    function onButtonReleased(int buttonEntityId, String entityName): void;

    // Called when the mouse enters a button's area
    function onButtonHoverEnter(int buttonEntityId, String entityName): void;

    // Called when the mouse leaves a button's area
    function onButtonHoverExit(int buttonEntityId, String entityName): void;
}
