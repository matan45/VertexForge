// IInputEventListener - Interface for receiving keyboard and mouse input events
// Implement this interface in your script class to receive input callbacks
// instead of polling every frame.
//
// Usage:
//   @Script
//   public class PlayerController implements IInputEventListener {
//       @Override
//       public function onKeyPressed(int keyCode, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void {
//           if (keyCode == Key::SPACE) {
//               Log::info("Space pressed!");
//           }
//       }
//       @Override
//       public function onKeyReleased(int keyCode, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void {}
//       @Override
//       public function onMouseButtonPressed(int button, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void {}
//       @Override
//       public function onMouseButtonReleased(int button, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void {}
//   }

interface IInputEventListener {
    function onKeyPressed(int keyCode, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void;
    function onKeyReleased(int keyCode, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void;
    function onMouseButtonPressed(int button, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void;
    function onMouseButtonReleased(int button, bool shift, bool ctrl, bool alt, float mouseX, float mouseY): void;
}
