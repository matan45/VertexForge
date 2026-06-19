// Loading - progress feedback for scene transitions & streaming (VK-1268).
//
// Read these from a loading-screen script to drive a progress bar + status
// text during scene loads, planet/biome transitions, and sector streaming.
// All values aggregate real engine progress (terrain tiles, sector entities,
// GPU/resource streaming) - nothing here is faked.
//
// Usage example (see game/LoadingScreen.mt for a full controller):
//   if (Loading::isActive()) {
//       UI::setProgressBarValue(barId, Loading::getProgress());
//       UI::setLabelText(statusId, Loading::getPhaseLabel());
//   }
public class Loading {
    public constructor() {
    }

    // True while any tracked subsystem still has loading work in flight
    // (scene entity load, terrain tile actions, GPU/resource streaming).
    public static function isActive(): bool {
        return _native_loading_isActive();
    }

    // Weighted overall progress 0..1 across the load phases
    // (terrain 0-40% / sectors 40-70% / gpu 70-90% / init 90-100%).
    // Returns 1.0 when idle.
    public static function getProgress(): float {
        return _native_loading_getProgress();
    }

    // Current phase as an int:
    //   0 = Idle, 1 = Terrain, 2 = Sectors, 3 = GpuStreaming,
    //   4 = Initializing, 5 = Complete
    public static function getPhase(): int {
        return _native_loading_getPhase();
    }

    // Human-readable status text for the current phase, e.g.
    // "Generating terrain..." / "Loading sectors..." / "Streaming resources...".
    public static function getPhaseLabel(): string {
        return _native_loading_getPhaseLabel();
    }

    // Raw scene-entity deserialization fraction 0..1 (smooth; the best signal
    // for new-game / load-game where a whole scene is being instantiated).
    public static function getSceneProgress(): float {
        return _native_loading_getSceneProgress();
    }

    // Terrain tile load/unload actions still queued (world / sector mode).
    public static function getPendingTileCount(): int {
        return _native_loading_getPendingTileCount();
    }

    // GPU uploads queued + CPU resource loads still in flight.
    public static function getStreamingPending(): int {
        return _native_loading_getStreamingPending();
    }
}
