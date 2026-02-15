// IUIProgressBarListener - Interface for receiving UI progress bar events
// Implement this interface in @Script classes to receive progress bar callbacks
// Unlike slider events, progress bar has no drag or hover events (non-interactive)
//
// Usage:
//   import engine::IUIProgressBarListener;
//
//   @Script
//   public class LoadingScreen implements IUIProgressBarListener {
//       @Override
//       public function onProgressBarValueChanged(int entityId, String entityName, float newValue, float previousValue): void {
//           Log::info("Progress: " + entityName + " -> " + newValue);
//       }
//
//       @Override
//       public function onProgressBarCompleted(int entityId, String entityName): void {
//           Log::info("Loading complete: " + entityName);
//       }
//   }

interface IUIProgressBarListener {
    // Called when the progress bar value changes
    function onProgressBarValueChanged(int entityId, String entityName, float newValue, float previousValue): void;

    // Called when the progress bar reaches its maximum value
    function onProgressBarCompleted(int entityId, String entityName): void;
}
