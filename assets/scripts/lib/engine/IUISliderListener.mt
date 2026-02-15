// IUISliderListener - Interface for receiving UI slider events
// Implement this interface in @Script classes to receive slider callbacks
// Unlike collision callbacks, slider events are broadcast to ALL scripts implementing this interface
//
// Usage:
//   import engine::IUISliderListener;
//
//   @Script
//   public class VolumeController implements IUISliderListener {
//       @Override
//       public function onSliderValueChanged(int entityId, String entityName, float newValue, float previousValue): void {
//           Log::info("Slider changed: " + entityName + " -> " + newValue);
//       }
//
//       @Override
//       public function onSliderDragStart(int entityId, String entityName): void { }
//       @Override
//       public function onSliderDragEnd(int entityId, String entityName, float finalValue): void { }
//       @Override
//       public function onSliderHoverEnter(int entityId, String entityName): void { }
//       @Override
//       public function onSliderHoverExit(int entityId, String entityName): void { }
//   }

interface IUISliderListener {
    // Called when the slider value changes
    function onSliderValueChanged(int entityId, String entityName, float newValue, float previousValue): void;

    // Called when the user starts dragging the slider handle
    function onSliderDragStart(int entityId, String entityName): void;

    // Called when the user stops dragging the slider handle
    function onSliderDragEnd(int entityId, String entityName, float finalValue): void;

    // Called when the mouse enters the slider's area
    function onSliderHoverEnter(int entityId, String entityName): void;

    // Called when the mouse leaves the slider's area
    function onSliderHoverExit(int entityId, String entityName): void;
}
