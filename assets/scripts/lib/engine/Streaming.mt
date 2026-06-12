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

    // ---- World Sector streaming sources ----
    // In a sector world, gameplay can register additional streaming sources so
    // sectors stream in around squads / command centers, not just the camera.
    //
    //   int srcId = Streaming::registerWorldSource(pos.x, pos.y, pos.z, 1.0, 2, unit.getUUID());
    //   Streaming::updateWorldSource(srcId, newPos.x, newPos.y, newPos.z);
    //   Streaming::unregisterWorldSource(srcId);   // or automatic when the owner entity dies

    // Register a gameplay streaming source. radiusMultiplier scales the world's
    // load/unload radii for this source; priority > 0 wins the per-frame load
    // budget over lower-priority sources (the camera is priority 0); a non-zero
    // ownerEntityUUID auto-unregisters the source when that entity is deleted.
    // Returns the source id, or 0 if no sector world is active.
    public static function registerWorldSource(float x, float y, float z,
                                               float radiusMultiplier, int priority,
                                               int ownerEntityUUID): int {
        return _native_streaming_registerWorldSource(x, y, z, radiusMultiplier, priority, ownerEntityUUID);
    }

    // Move a streaming source (call from movement ticks)
    public static function updateWorldSource(int sourceId, float x, float y, float z): void {
        _native_streaming_updateWorldSource(sourceId, x, y, z);
    }

    // Remove a streaming source explicitly
    public static function unregisterWorldSource(int sourceId): void {
        _native_streaming_unregisterWorldSource(sourceId);
    }

    // Whether a previously registered source still exists
    public static function isWorldSourceValid(int sourceId): bool {
        return _native_streaming_isWorldSourceValid(sourceId);
    }

    // Whether the sector containing the given world position is fully loaded.
    // Use to gate AI activation / spawning on streamed content being present.
    // Returns true when no sector world is active.
    public static function isSectorLoadedAt(float worldX, float worldZ): bool {
        return _native_streaming_isSectorLoadedAt(worldX, worldZ);
    }
}
