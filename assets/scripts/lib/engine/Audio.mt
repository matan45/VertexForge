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
//   Audio::setBusVolume("Music", 0.8);  // Set Music bus to 80%

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

    // ============================================
    // Distance Filter (VK-920)
    // ============================================

    // Set distance-based low-pass filter parameters on a 3D audio source
    // Simulates air absorption of high frequencies over distance
    // enabled: toggle filter on/off
    // startDistance: distance at which filtering begins
    // maxDistance: distance at which filter reaches full strength
    // intensity: strength of the low-pass effect (0.0 to 1.0)
    public static function setDistanceFilter(int entityId, bool enabled, float startDistance, float maxDistance, float intensity): void {
        _native_audio_setDistanceFilter(entityId, enabled, startDistance, maxDistance, intensity);
    }

    // Check if distance filter is enabled on a 3D audio source
    public static function getDistanceFilterEnabled(int entityId): bool {
        return _native_audio_getDistanceFilterEnabled(entityId);
    }

    // Get distance filter intensity (0.0 to 1.0)
    public static function getFilterIntensity(int entityId): float {
        return _native_audio_getFilterIntensity(entityId);
    }

    // ============================================
    // Cone Attenuation (VK-918)
    // ============================================

    // Set cone angles for directional audio (3D sources only)
    // innerAngle: full volume within this angle (0-360, 360 = omnidirectional)
    // outerAngle: volume fades to outerGain between inner and outer angles (0-360)
    public static function setConeAngles(int entityId, float innerAngle, float outerAngle): void {
        _native_audio_setConeAngles(entityId, innerAngle, outerAngle);
    }

    // Get inner cone angle in degrees (0-360)
    public static function getConeInnerAngle(int entityId): float {
        return _native_audio_getConeInnerAngle(entityId);
    }

    // Get outer cone angle in degrees (0-360)
    public static function getConeOuterAngle(int entityId): float {
        return _native_audio_getConeOuterAngle(entityId);
    }

    // Set volume multiplier outside the outer cone (0.0 = silent, 1.0 = full)
    public static function setConeOuterGain(int entityId, float gain): void {
        _native_audio_setConeOuterGain(entityId, gain);
    }

    // Get volume multiplier outside the outer cone
    public static function getConeOuterGain(int entityId): float {
        return _native_audio_getConeOuterGain(entityId);
    }

    // ============================================
    // Audio Bus Assignment (VK-916)
    // ============================================

    // Set the audio bus for an entity's audio component
    // busName: name of the bus (e.g., "Master", "Music", "SFX", "Dialogue", "Ambient")
    public static function setBus(int entityId, string busName): void {
        _native_audio_setBus(entityId, busName);
    }

    // Get the audio bus name for an entity's audio component
    public static function getBus(int entityId): string {
        return _native_audio_getBus(entityId);
    }

    // ============================================
    // Audio Bus Mixing (VK-916)
    // ============================================

    // Set volume for an audio bus (affects all sources on that bus)
    // busName: name of the bus
    // volume: 0.0 to 1.0
    public static function setBusVolume(string busName, float volume): void {
        _native_audio_setBusVolume(busName, volume);
    }

    // Get current volume of an audio bus
    public static function getBusVolume(string busName): float {
        return _native_audio_getBusVolume(busName);
    }

    // Mute or unmute an audio bus
    // When muted, all sources on the bus are silenced
    public static function muteBus(string busName, bool muted): void {
        _native_audio_muteBus(busName, muted);
    }

    // ============================================
    // Mix Snapshots (VK-916)
    // ============================================

    // Save current bus volume/mute states as a named snapshot
    public static function saveSnapshot(string name): void {
        _native_audio_saveSnapshot(name);
    }

    // Restore bus states from a previously saved snapshot
    public static function loadSnapshot(string name): void {
        _native_audio_loadSnapshot(name);
    }

    // ============================================
    // Audio Effects (VK-917)
    // ============================================

    // Add an effect to a bus (Reverb, EQ, Compressor, Echo, Chorus)
    // Returns effect ID (> 0) on success, 0 if at max or EFX unavailable
    public static function addBusEffect(string busName, string effectType): int {
        return _native_audio_addBusEffect(busName, effectType);
    }

    // Remove an effect from a bus by its effect ID
    public static function removeBusEffect(string busName, int effectId): void {
        _native_audio_removeBusEffect(busName, effectId);
    }

    // Enable or disable an effect on a bus
    public static function setBusEffectEnabled(string busName, int effectId, bool enabled): void {
        _native_audio_setBusEffectEnabled(busName, effectId, enabled);
    }

    // Set wet/dry mix for an effect (0.0 = fully dry, 1.0 = fully wet)
    public static function setBusEffectWetDry(string busName, int effectId, float mix): void {
        _native_audio_setBusEffectWetDry(busName, effectId, mix);
    }

    // Set reverb preset by name (e.g., "Cave", "Concert Hall", "Bathroom")
    public static function setReverbPreset(string busName, int effectId, string presetName): void {
        _native_audio_setReverbPreset(busName, effectId, presetName);
    }
}
