// Streaming - Static utility class for level streaming
// Manages proximity-based automatic scene loading/unloading
//
// Usage examples:
//   // Auto-load a scene when player enters an AABB zone
//   var zoneId = Streaming::setTriggerZone(-100, -50, -100, 100, 50, 100, "assets/scenes/dungeon.vfScene");
//
//   // Remove the trigger zone
//   Streaming::removeTriggerZone(zoneId);
//
//   // Preload a scene in the background (not yet instantiated)
//   Streaming::preload("assets/scenes/boss_arena.vfScene");
//
//   // Check if a scene is currently loaded
//   bool loaded = Streaming::isLoaded("boss_arena");

public class Streaming {
    public constructor() {
    }

    // Set up a trigger zone that auto-loads a scene when the camera enters the AABB
    // The scene is automatically unloaded when the camera leaves the zone
    // Returns a zone ID that can be used to remove the zone later
    public static function setTriggerZone(float minX, float minY, float minZ,
                                          float maxX, float maxY, float maxZ,
                                          string scenePath): int {
        return _native_streaming_setTriggerZone(minX, minY, minZ, maxX, maxY, maxZ, scenePath);
    }

    // Remove a previously created trigger zone by its ID
    // If the zone's scene was loaded, it will be unloaded
    public static function removeTriggerZone(int zoneId): void {
        _native_streaming_removeTriggerZone(zoneId);
    }

    // Preload a scene file in the background for faster future loading
    // The scene is parsed but not yet instantiated into the world
    public static function preload(string scenePath): void {
        _native_streaming_preload(scenePath);
    }

    // Check if a scene is currently loaded (by scene name)
    public static function isLoaded(string sceneName): bool {
        return _native_streaming_isLoaded(sceneName);
    }
}
