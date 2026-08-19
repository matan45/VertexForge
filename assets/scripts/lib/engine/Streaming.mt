// Streaming - Static utility class for level streaming
// Manages proximity-based automatic scene loading/unloading
//
// Usage examples:
//   // Auto-load a scene when player enters an AABB zone
//   int zoneId = Streaming::setTriggerZone(-100, -50, -100, 100, 50, 100, "assets/scenes/dungeon.vfScene");
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
    //   int srcId = Streaming::registerWorldSource(pos.x, pos.y, pos.z, 0.5, 0,
    //                                              Entity::getUUID(baseId));
    //   Streaming::updateWorldSource(srcId, newPos.x, newPos.y, newPos.z);
    //   Streaming::unregisterWorldSource(srcId);   // or automatic when the owner entity dies
    //
    // Two rules that are easy to get wrong:
    //
    //  * PRIORITY. The camera is priority 0 and the default per-frame load budget is a
    //    single sector, so a priority > 0 source out-bids the player's own view for that
    //    slot. Persistent gameplay sources (bases, objectives) belong at priority 0;
    //    reserve priority > 0 for short-lived sources whose destination the camera is
    //    about to reach anyway, such as a minimap-jump pre-warm.
    //
    //  * THREADING. Per-frame updateWorldSource calls belong in onLateUpdate, not onUpdate:
    //    onUpdate runs on a worker thread concurrently with the sector streamer, while
    //    onLateUpdate is ordered after it. The service locks its source table, so either is
    //    correct - this only keeps a per-frame writer off the streamer's back. One-shot
    //    register/unregister calls are fine from anywhere, including onStart.
    //
    // TARGET STATE (registerWorldSourceEx). A normal source asks for sectors to be ACTIVATED:
    // read, parsed, and spawned as live entities with physics bodies and GPU slots. A source
    // registered with targetState 1 asks only for PREFETCH: the sector's bytes come into memory
    // and nothing spawns, so a later activation costs no disk read. That is what a speculative
    // pre-warm wants - a minimap hover, or a destination the player has not committed to yet -
    // because it costs bytes instead of live objects. Use registerWorldSource (targetState 2)
    // whenever the source represents something that actually needs the world to exist around it.

    // Target states for registerWorldSourceEx
    //   1 = prefetch only: bytes resident, no entities spawned
    //   2 = activate: entities spawned (what registerWorldSource does)

    // Register a gameplay streaming source. radiusMultiplier scales the world's
    // load AND unload radii for this source, so a source also pins the sectors it
    // covers against eviction; priority > 0 wins the per-frame load budget over
    // lower-priority sources (the camera is priority 0); a non-zero ownerEntityUUID
    // (from Entity::getUUID) auto-unregisters the source when that entity is deleted.
    // Returns the source id, or 0 if no sector world is active.
    public static function registerWorldSource(float x, float y, float z,
                                               float radiusMultiplier, int priority,
                                               int ownerEntityUUID): int {
        return _native_streaming_registerWorldSource(x, y, z, radiusMultiplier, priority, ownerEntityUUID);
    }

    // As registerWorldSource, but caps what this source may ask a sector to become:
    // targetState 1 = prefetch only (bytes resident, no entities), 2 = activate.
    // Anything else is treated as 2. Returns the source id, or 0 if no sector world is active.
    public static function registerWorldSourceEx(float x, float y, float z,
                                                 float radiusMultiplier, int priority,
                                                 int ownerEntityUUID, int targetState): int {
        return _native_streaming_registerWorldSourceEx(x, y, z, radiusMultiplier, priority,
                                                       ownerEntityUUID, targetState);
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
