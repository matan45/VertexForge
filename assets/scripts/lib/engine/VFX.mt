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

    // Create or retrieve a persistent GPU-only listener for a compatible
    // deterministic one-shot asset. Returns 0 when the asset is unsupported or
    // the listener cannot be allocated. The authored burst determines how many
    // particles each request produces.
    public static function createChannel(string path): int {
        return _native_vfx_createChannel(path);
    }

    // Create a channel with an explicit particle count per request.
    public static function createChannelWithCount(string path, int particlesPerRequest): int {
        return _native_vfx_createChannelWithCount(path, particlesPerRequest);
    }

    // Submit one world-space spawn request with authored color, unit scale, and
    // no direction override. Requests may be dropped when GPU capacity is full.
    public static function channelEmit(int channelId, float x, float y, float z): void {
        _native_vfx_channelEmit(channelId, x, y, z);
    }

    // Submit one request with a multiplicative RGBA tint. Components are clamped
    // to [0, 1]; alpha participates in the multiplication.
    public static function channelEmitTinted(int channelId, float x, float y, float z,
            float r, float g, float b, float a): void {
        _native_vfx_channelEmitTinted(channelId, x, y, z, r, g, b, a);
    }

    // Submit a fully specified request. Position and direction are world-space;
    // scale multiplies shape extent and particle size. White RGBA is neutral.
    public static function channelEmitFull(int channelId, float x, float y, float z,
            float scale, float dx, float dy, float dz,
            float r, float g, float b, float a): void {
        _native_vfx_channelEmitFull(channelId, x, y, z, scale, dx, dy, dz, r, g, b, a);
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

    // Attach a spawned instance to an entity socket. The instance follows the
    // socket's world transform (position + rotation) each frame with no further
    // scripting. The attachment auto-clears when the entity or socket becomes
    // invalid, or when the instance is destroyed. Call detach to stop following.
    public static function attachToSocket(int instanceId, int parentEntityId, string socketName): void {
        _native_vfx_attachToSocket(instanceId, parentEntityId, socketName);
    }

    // Stop a spawned instance from following its socket. The instance stays at
    // its last position.
    public static function detach(int instanceId): void {
        _native_vfx_detach(instanceId);
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

    // ============================================
    // Combo Sequences (.vfVFXSequence — VK-1425)
    // A combo plays several .vfVFX steps at scheduled times/cues from one
    // .vfVFXSequence asset. The returned id is a combo id, distinct from the
    // per-instance ids above; pass it to the other combo functions.
    // ============================================

    // Spawn a combo at a world position and play it. Auto-destroys once every
    // step has finished. Returns the combo id (0 on failure).
    public static function spawnCombo(string path, float x, float y, float z): int {
        return _native_vfx_spawnCombo(path, x, y, z, false);
    }

    // Spawn a combo that stays alive after its steps finish (caller owns it and
    // must call destroyCombo). Useful for combos with looping steps.
    public static function spawnComboLooping(string path, float x, float y, float z): int {
        return _native_vfx_spawnCombo(path, x, y, z, true);
    }

    // Destroy a combo and all of its child effects.
    public static function destroyCombo(int comboId): void {
        _native_vfx_destroyCombo(comboId);
    }

    // Stop a combo: non-looping children finish naturally, looping children are destroyed.
    public static function stopCombo(int comboId): void {
        _native_vfx_stopCombo(comboId);
    }

    // Reset a combo back to its start (destroys children, rewinds to time 0).
    public static function resetCombo(int comboId): void {
        _native_vfx_resetCombo(comboId);
    }

    // Move a combo to a new world position (children keep their per-step offsets).
    public static function setComboPosition(int comboId, float x, float y, float z): void {
        _native_vfx_setComboPosition(comboId, x, y, z);
    }

    // Attach the whole combo to an entity socket; children follow the socket each
    // frame while keeping their per-step local offsets.
    public static function attachComboToSocket(int comboId, int parentEntityId, string socketName): void {
        _native_vfx_attachComboToSocket(comboId, parentEntityId, socketName);
    }

    // Stop a combo from following its socket.
    public static function detachCombo(int comboId): void {
        _native_vfx_detachCombo(comboId);
    }

    // Fire all cue-driven steps in the combo whose cue name matches.
    // Listen for cues (from these triggers or authored timeline markers) by
    // implementing IVFXComboCueListener.onComboCue in an @Script class (VK-1495).
    public static function triggerComboCue(int comboId, string cueName): void {
        _native_vfx_triggerComboCue(comboId, cueName);
    }

    // Fire a cue with typed payload data for child offsets/tints and listeners.
    public static function triggerComboCuePayload(int comboId, string cueName,
            float x, float y, float z, float r, float g, float b, float a, float scalar): void {
        _native_vfx_triggerComboCuePayload(comboId, cueName, x, y, z, r, g, b, a, scalar);
    }

    // Check whether a combo is still playing (has unfinished steps).
    public static function comboIsPlaying(int comboId): bool {
        return _native_vfx_comboIsPlaying(comboId);
    }

    // ============================================
    // Deterministic transport (VK-1451)
    // Stable runtime controls. Live mid-game seek is intentionally not exposed:
    // GPU particles already in flight cannot be visually rewound, so a scripted
    // seek would mislead. Seeking is an editor-preview / authoring capability.
    // ============================================

    // Spawn a combo with an explicit RNG seed so its emission schedule is
    // reproducible across runs. Returns the combo id (0 on failure).
    public static function spawnComboSeeded(string path, float x, float y, float z, int seed): int {
        return _native_vfx_spawnComboSeeded(path, x, y, z, seed);
    }

    // Spawn a combo and fast-forward its schedule by `prewarm` seconds before the
    // first visible frame (e.g. a fire that should already be burning on spawn).
    public static function spawnComboPrewarmed(string path, float x, float y, float z, float prewarm): int {
        return _native_vfx_spawnComboPrewarmed(path, x, y, z, prewarm);
    }

    // Pause a combo's timeline (children freeze where they are).
    public static function pauseCombo(int comboId): void {
        _native_vfx_pauseCombo(comboId);
    }

    // Resume a paused combo.
    public static function resumeCombo(int comboId): void {
        _native_vfx_resumeCombo(comboId);
    }

    // Set the combo's playback rate (1.0 = normal, 2.0 = double speed, etc.).
    public static function setComboRate(int comboId, float rate): void {
        _native_vfx_setComboRate(comboId, rate);
    }
}
