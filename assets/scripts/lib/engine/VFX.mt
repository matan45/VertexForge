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
}
