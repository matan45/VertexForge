// Stats - runtime performance counters (VK-1534)
//
// Reads the engine's runtime-safe stat sinks so a shipped game can build a perf HUD.
// getGpuMs() reads 0 until the first GPU timestamp readback is ready (a few frames)
// or on a device without timestamp support. See PerfHud for a ready-made overlay.
//
// Usage:
//   float fps = Stats::getFps();
//   string line = Stats::getHudLine();   // "FPS 60  CPU 16.7ms  GPU 12.3ms  Draws 842"
//   Stats::setHudEnabled(true);          // persisted; off by default

public class Stats {
    public constructor() {
    }

    public static function getFps(): float {
        return _native_stats_getFps();
    }

    public static function getCpuMs(): float {
        return _native_stats_getCpuMs();
    }

    public static function getGpuMs(): float {
        return _native_stats_getGpuMs();
    }

    public static function getDrawCalls(): int {
        return _native_stats_getDrawCalls();
    }

    // Preformatted one-line summary, for a single UILabel.
    public static function getHudLine(): string {
        return _native_stats_getHudLine();
    }

    // Persisted overlay toggle (config key gfx.hud); off by default.
    public static function getHudEnabled(): bool {
        return _native_stats_getHudEnabled();
    }

    public static function setHudEnabled(bool enabled): void {
        _native_stats_setHudEnabled(enabled);
    }
}
