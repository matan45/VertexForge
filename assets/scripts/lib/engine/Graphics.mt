// Graphics - runtime graphics quality settings (VK-1534)
//
// Apply a quality preset plus present-mode (VSync) and MSAA from inside a shipped
// game, with no editor involvement. The choice is persisted to config.json (next to
// the game's saves) and re-applied automatically on the next launch, overriding the
// scene-baked defaults.
//
// Usage:
//   Graphics::applyPreset(Graphics::PRESET_HIGH);
//   Graphics::setPresentMode(Graphics::PRESENT_FIFO);   // VSync on
//   Graphics::setMsaa(Graphics::MSAA_4X);
//   int current = Graphics::getPreset();

public class Graphics {
    // RenderPreset ordinals
    public static final int PRESET_LOW = 0;
    public static final int PRESET_MEDIUM = 1;
    public static final int PRESET_HIGH = 2;
    public static final int PRESET_ULTRA = 3;
    public static final int PRESET_CUSTOM = 4;

    // PresentMode ordinals
    public static final int PRESENT_FIFO = 0;       // VSync on (capped to refresh, no tearing)
    public static final int PRESENT_MAILBOX = 1;    // low-latency triple buffer, no tearing
    public static final int PRESENT_IMMEDIATE = 2;  // uncapped (may tear)

    // MsaaSamples ordinals (clamped to device support when applied)
    public static final int MSAA_OFF = 0;
    public static final int MSAA_2X = 1;
    public static final int MSAA_4X = 2;
    public static final int MSAA_8X = 3;

    public constructor() {
    }

    // Apply Low/Medium/High/Ultra now and persist it. Present-mode and MSAA keep their
    // own persisted values, so changing preset does not reset the player's VSync/AA.
    public static function applyPreset(int preset): void {
        _native_graphics_applyPreset(preset);
    }

    public static function setPresentMode(int mode): void {
        _native_graphics_setPresentMode(mode);
    }

    public static function setMsaa(int samples): void {
        _native_graphics_setMsaa(samples);
    }

    public static function getPreset(): int {
        return _native_graphics_getPreset();
    }

    public static function getPresentMode(): int {
        return _native_graphics_getPresentMode();
    }

    public static function getMsaa(): int {
        return _native_graphics_getMsaa();
    }
}
