// IUITextInputListener - Interface for receiving UI text input events
// Implement this interface in @Script classes to receive text input callbacks
// Text input events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUITextInputListener;
//
//   @Script
//   public class FormController implements IUITextInputListener {
//       @Override
//       public function onTextInputSubmit(int entityId, String entityName, String text): void {
//           Log::info("Submitted: " + text);
//       }
//
//       @Override
//       public function onTextInputChanged(int entityId, String entityName, String text): void { }
//       @Override
//       public function onTextInputFocused(int entityId, String entityName): void { }
//       @Override
//       public function onTextInputUnfocused(int entityId, String entityName): void { }
//   }

interface IUITextInputListener {
    // Called when the user presses Enter in a text input field
    function onTextInputSubmit(int entityId, String entityName, String text): void;

    // Called whenever the text content changes (typing, pasting, deleting)
    function onTextInputChanged(int entityId, String entityName, String text): void;

    // Called when a text input field gains focus (clicked or tabbed into)
    function onTextInputFocused(int entityId, String entityName): void;

    // Called when a text input field loses focus (clicked outside, Escape, or Tab)
    function onTextInputUnfocused(int entityId, String entityName): void;
}
