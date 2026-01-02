// Audio - Static utility class for audio component operations
// Works with entity IDs (int) to control audio playback
//
// Usage examples:
//   int self = Entity::self();
//   Audio::play2d(self);           // Play 2D audio on entity
//   Audio::play3d(self);           // Play 3D positional audio
//   Audio::stop(self);             // Stop audio playback
//   Audio::setVolume(self, 0.5);   // Set volume to 50%
//   bool playing = Audio::isPlaying(self);

public class Audio {
    public constructor() {
    }

    // ============================================
    // Playback Control
    // ============================================

    // Play 2D audio (streaming, for music/ambient)
    // Entity must have AudioSource2D component with audio file set
    // Returns audio handle (> 0) on success, 0 on failure
    public static function play2d(int entityId): int {
        return _native_audio_play2d(entityId);
    }

    // Play 3D positional audio (cached, for sound effects)
    // Entity must have AudioSource3D component with audio file set
    // Returns audio handle (> 0) on success, 0 on failure
    public static function play3d(int entityId): int {
        return _native_audio_play3d(entityId);
    }

    // Stop audio playback on entity
    // Works for both 2D and 3D audio components
    public static function stop(int entityId): void {
        _native_audio_stop(entityId);
    }

    // Pause audio playback on entity
    public static function pause(int entityId): void {
        _native_audio_pause(entityId);
    }

    // Resume paused audio on entity
    public static function resume(int entityId): void {
        _native_audio_resume(entityId);
    }

    // Check if audio is currently playing on entity
    public static function isPlaying(int entityId): bool {
        return _native_audio_isPlaying(entityId);
    }

    // ============================================
    // Volume & Pitch
    // ============================================

    // Get current volume (0.0 to 1.0)
    public static function getVolume(int entityId): float {
        return _native_audio_getVolume(entityId);
    }

    // Set volume (0.0 to 1.0)
    // Updates component and live audio if playing
    public static function setVolume(int entityId, float volume): void {
        _native_audio_setVolume(entityId, volume);
    }

    // Get current pitch (0.5 to 2.0)
    public static function getPitch(int entityId): float {
        return _native_audio_getPitch(entityId);
    }

    // Set pitch (0.5 to 2.0)
    // Updates component and live audio if playing
    public static function setPitch(int entityId, float pitch): void {
        _native_audio_setPitch(entityId, pitch);
    }

    // ============================================
    // Loop Control
    // ============================================

    // Check if loop is enabled
    public static function getLoop(int entityId): bool {
        return _native_audio_getLoop(entityId);
    }

    // Set loop enabled/disabled
    // Note: Takes effect on next play() call
    public static function setLoop(int entityId, bool loop): void {
        _native_audio_setLoop(entityId, loop);
    }
}
