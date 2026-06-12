// VFX - Static utility class for VFX component operations
// Works with entity IDs (int) to control VFX playback
//
// Usage examples:
//   int self = Entity::self();
//   VFX::play(self);              // Start VFX playback on entity
//   VFX::stop(self);              // Stop VFX playback
//   VFX::setLoop(self, true);     // Enable looping
//   bool playing = VFX::isPlaying(self);
//
//   // Load a different VFX asset
//   VFX::setPath(self, "effects/explosion.vfVFX");
//
//   // Fire-and-forget effect at a world position (no entity needed):
//   VFX::spawnAt("effects/explosion.vfVFX", hit.x, hit.y, hit.z);
//
//   // Looping instance controlled manually:
//   int fireId = VFX::spawnAtLooping("effects/fire.vfVFX", x, y, z);
//   VFX::setInstancePosition(fireId, x2, y2, z2);
//   VFX::destroyInstance(fireId);

public class VFX {
    public constructor() {
    }

    // ============================================
    // Playback Control
    // ============================================

    // Start VFX playback on entity
    // Entity must have VFXComponent with vfxPath set
    public static function play(int entityId): void {
        _native_vfx_play(entityId);
    }

    // Stop VFX playback on entity
    public static function stop(int entityId): void {
        _native_vfx_stop(entityId);
    }

    // Reset VFX instance (stop and reinitialize)
    public static function reset(int entityId): void {
        _native_vfx_reset(entityId);
    }

    // Check if VFX is currently playing on entity
    public static function isPlaying(int entityId): bool {
        return _native_vfx_isPlaying(entityId);
    }

    // ============================================
    // Loop Control
    // ============================================

    // Check if loop is enabled
    public static function getLoop(int entityId): bool {
        return _native_vfx_getLoop(entityId);
    }

    // Set loop enabled/disabled
    public static function setLoop(int entityId, bool loop): void {
        _native_vfx_setLoop(entityId, loop);
    }

    // ============================================
    // VFX Asset Path
    // ============================================

    // Get the current VFX asset path
    public static function getPath(int entityId): string {
        return _native_vfx_getPath(entityId);
    }

    // Set a new VFX asset path (load from path)
    // This will stop any current playback and load the new VFX
    public static function setPath(int entityId, string path): void {
        _native_vfx_setPath(entityId, path);
    }

    // ============================================
    // Auto-Play Control
    // ============================================

    // Check if auto-play is enabled
    public static function getAutoPlay(int entityId): bool {
        return _native_vfx_getAutoPlay(entityId);
    }

    // Set auto-play enabled/disabled
    // When enabled, VFX starts automatically when entity becomes active
    public static function setAutoPlay(int entityId, bool autoPlay): void {
        _native_vfx_setAutoPlay(entityId, autoPlay);
    }

    // ============================================
    // Instance Spawning (fire-and-forget, no entity)
    // ============================================

    // Spawn a one-shot effect at a world position and play it immediately.
    // The instance destroys itself once finished. Returns the instance id
    // (0 on failure); keep it only if you want to move/stop it early.
    public static function spawnAt(string path, float x, float y, float z): int {
        return _native_vfx_spawnAt(path, x, y, z, false);
    }

    // Spawn a looping effect at a world position and play it immediately.
    // Caller owns the instance and must call destroyInstance when done.
    public static function spawnAtLooping(string path, float x, float y, float z): int {
        return _native_vfx_spawnAt(path, x, y, z, true);
    }

    // Destroy a spawned instance
    public static function destroyInstance(int instanceId): void {
        _native_vfx_destroyInstance(instanceId);
    }

    // Stop emission on a spawned instance (existing particles finish naturally)
    public static function stopInstance(int instanceId): void {
        _native_vfx_stopInstance(instanceId);
    }

    // Move a spawned instance to a new world position
    public static function setInstancePosition(int instanceId, float x, float y, float z): void {
        _native_vfx_setInstancePosition(instanceId, x, y, z);
    }

    // Check whether a spawned instance is still playing
    public static function instanceIsPlaying(int instanceId): bool {
        return _native_vfx_instanceIsPlaying(instanceId);
    }

    // ============================================
    // Runtime Overrides (per instance)
    // ============================================

    // Override a scalar emitter parameter on a live instance. Returns false
    // for unknown names. Names: spawnRate, lifetime, startSize, startSpeed,
    // stretchMultiplier, windStrength, gravityStrength, softParticleDistance,
    // lightingInfluence, collisionLifetimeLoss, coneSpread, renderMode,
    // collisionEnabled (0/1)
    public static function setOverride(int instanceId, string name, float value): bool {
        return _native_vfx_setOverride(instanceId, name, value);
    }

    // Override a vec3 emitter parameter on a live instance.
    // Names: emitDirection, windDirection, gravityDirection, shapeDimensions
    public static function setOverrideVec3(int instanceId, string name, float x, float y, float z): bool {
        return _native_vfx_setOverrideVec(instanceId, name, x, y, z);
    }

    // Override the start color (RGBA) on a live instance - e.g. team tinting
    public static function setOverrideColor(int instanceId, float r, float g, float b, float a): bool {
        return _native_vfx_setOverrideVec(instanceId, "startColor", r, g, b, a);
    }
}
