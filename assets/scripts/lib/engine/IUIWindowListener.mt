// IUIWindowListener - Interface for receiving UI window events
// Implement this interface in @Script classes to receive window callbacks
// Window events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUIWindowListener;
//
//   @Script
//   public class PauseMenuController implements IUIWindowListener {
//       @Override
//       public function onWindowOpened(int windowEntityId, string entityName): void { }
//
//       @Override
//       public function onWindowClosed(int windowEntityId, string entityName): void {
//           Log::info("Window closed: " + entityName);
//       }
//   }

interface IUIWindowListener {
    // Called when a window opens (UI::openWindow)
    function onWindowOpened(int windowEntityId, string entityName): void;

    // Called when a window closes (UI::closeWindow or its title-bar close button)
    function onWindowClosed(int windowEntityId, string entityName): void;
}
