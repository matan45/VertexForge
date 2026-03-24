// ISceneEventListener - Interface for receiving scene lifecycle events
// Implement this interface in @Script classes to receive callbacks
// when scenes are loaded or unloaded
//
// Usage:
//   @Script
//   public class GameManager implements ISceneEventListener {
//       @Override
//       public function onSceneLoaded(string sceneName): void {
//           Log::info("Scene loaded: " + sceneName);
//       }
//
//       @Override
//       public function onSceneUnloaded(string sceneName): void {
//           Log::info("Scene unloaded: " + sceneName);
//       }
//   }

interface ISceneEventListener {
    // Called when a scene finishes loading
    // sceneName is the path for main scene loads, or the additive scene name
    function onSceneLoaded(string sceneName): void;

    // Called when a scene is unloaded
    // sceneName is the additive scene name, or empty string for main scene clear
    function onSceneUnloaded(string sceneName): void;
}
