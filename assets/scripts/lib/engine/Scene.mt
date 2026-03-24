// Scene - Static utility class for scene management
// Provides runtime scene loading, unloading, and additive scene support
//
// Usage examples:
//   Scene::load("assets/scenes/level1.vfScene");              // Replace current scene
//   var name = Scene::loadAdditive("assets/scenes/ui.vfScene"); // Load additively
//   Scene::unload(name);                                       // Unload additive scene
//   Scene::loadAsync("assets/scenes/level2.vfScene", lambda(bool success, string path) {
//       Log::info("Scene loaded: " + path);
//   });
//   var active = Scene::getActive();
//   Scene::setActive("ui");
//   bool loaded = Scene::isLoaded("ui");

public class Scene {
    public constructor() {
    }

    // Load a scene, replacing the current one entirely
    // All existing entities are destroyed before loading
    public static function load(string path): void {
        _native_scene_load(path);
    }

    // Load a scene additively without replacing the current one
    // Entities from the loaded scene are added alongside existing entities
    // Returns the auto-generated scene name (used for unload/setActive)
    public static function loadAdditive(string path): string {
        return _native_scene_loadAdditive(path);
    }

    // Unload a previously additively loaded scene by name
    // The name is returned by loadAdditive()
    public static function unload(string name): void {
        _native_scene_unload(name);
    }

    // Load a scene asynchronously with a completion callback
    // callback receives (bool success, string scenePath)
    public static function loadAsync(string path, lambda callback): void {
        _native_scene_loadAsync(path, callback);
    }

    // Get the name of the currently active scene
    // Default is "Main" for the primary scene
    public static function getActive(): string {
        return _native_scene_getActive();
    }

    // Set which scene is considered "active"
    // Affects which scene new entities are created in by default
    public static function setActive(string name): void {
        _native_scene_setActive(name);
    }

    // Check if a scene with the given name is currently loaded
    // "Main" is always loaded
    public static function isLoaded(string name): bool {
        return _native_scene_isLoaded(name);
    }
}
