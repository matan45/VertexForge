// IInputActionListener - Interface for receiving action-level input callbacks
// Implement this interface to get notified when registered actions are triggered,
// instead of polling InputAction::isPressed() in onUpdate.
//
// Usage:
//   @Script
//   public class PlayerController implements IInputActionListener {
//       public function onStart(): void {
//           InputAction::register("Jump", BindingType::KEY, Key::SPACE);
//           InputAction::register("Shoot", BindingType::MOUSE_BUTTON, Mouse::LEFT);
//       }
//       @Override
//       public function onActionPressed(String actionName): void {
//           if (actionName == "Jump") { Log::info("Jump!"); }
//       }
//       @Override
//       public function onActionReleased(String actionName): void {}
//   }

interface IInputActionListener {
    function onActionPressed(String actionName): void;
    function onActionReleased(String actionName): void;
}
