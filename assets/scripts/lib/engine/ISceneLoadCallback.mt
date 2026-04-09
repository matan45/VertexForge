// ISceneLoadCallback - Functional interface for async scene load completion
//
// Usage:
//   Scene::loadAsync("scenes/Level.vfScene", (bool success, string path) -> {
//       if (success) { Log::info("Loaded: " + path); }
//   });

public interface ISceneLoadCallback {
    function onSceneLoaded(bool success, string path): void;
}
