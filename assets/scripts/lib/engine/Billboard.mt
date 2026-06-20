// Billboard - Static utility class for per-entity billboard markers.
// Camera-facing sprites for RTS selection rings, health bars, status icons, etc.
// Markers created here render in Play mode (worldMarker billboards go through the
// GPU billboard path); plain editor debug icons are not affected.
//
// Usage examples:
//   int self = Entity::self();
//   Billboard::ensure(self);                       // add/promote a world marker
//   Billboard::setTexture(self, "ui/ring.vfImage"); // custom sprite
//   Billboard::setSize(self, 1.5, 1.5);             // world-space size (units)
//   Billboard::setTint(self, 0.0, 1.0, 0.0, 1.0);   // green
//   Billboard::setFlipbook(self, 4, 4, 12.0);       // 4x4 sheet @ 12 fps
//   Billboard::setLoop(self, false);                // play once then hold last frame
//   Billboard::restart(self);                       // re-anchor animation to "now"
//   Billboard::setSpin(self, 1.0);                  // 1 rad/sec spin
//   Billboard::setVisible(self, false);             // hide in Play

public class Billboard {
    public constructor() {
    }

    // ============================================
    // Lifecycle
    // ============================================

    // Add a BillboardComponent if absent and configure it as a play-mode world
    // marker (renders while playing). Idempotent: calling again only promotes an
    // existing component without clobbering its other fields. Returns true on a
    // valid entity.
    public static function ensure(int entityId): bool {
        return _native_billboard_ensure(entityId);
    }

    // Remove the BillboardComponent from the entity. Returns true if one was removed.
    public static function remove(int entityId): bool {
        return _native_billboard_remove(entityId);
    }

    // True if the entity currently has a play-mode billboard marker.
    public static function has(int entityId): bool {
        return _native_billboard_has(entityId);
    }

    // ============================================
    // Appearance
    // ============================================

    // Point the billboard at a .vfImage sprite asset (path must be registered).
    public static function setTexture(int entityId, string vfImagePath): void {
        _native_billboard_setTexture(entityId, vfImagePath);
    }

    // RGBA tint / opacity multiplier applied to the sprite.
    public static function setTint(int entityId, float r, float g, float b, float a): void {
        _native_billboard_setTint(entityId, r, g, b, a);
    }

    // Billboard size (world-space units for world markers).
    public static function setSize(int entityId, float w, float h): void {
        _native_billboard_setSize(entityId, w, h);
    }

    // ============================================
    // Animation
    // ============================================

    // Flipbook sprite-sheet animation. cols/rows describe the atlas grid (1x1 =
    // no flipbook); frameRate is frames/sec (<=0 holds the first frame).
    public static function setFlipbook(int entityId, int cols, int rows, float frameRate): void {
        _native_billboard_setFlipbook(entityId, cols, rows, frameRate);
    }

    // UV scroll speed in units/sec along U and V.
    public static function setScroll(int entityId, float u, float v): void {
        _native_billboard_setScroll(entityId, u, v);
    }

    // Scale-throb: amplitude (fraction) and frequency (rad/sec). amplitude 0 = off.
    public static function setPulse(int entityId, float amplitude, float frequency): void {
        _native_billboard_setPulse(entityId, amplitude, frequency);
    }

    // Spin about the view normal, in radians/sec. 0 = no spin.
    public static function setSpin(int entityId, float radPerSec): void {
        _native_billboard_setSpin(entityId, radPerSec);
    }

    // Flipbook playback mode. true = loop continuously (default); false = play the
    // sprite sheet once then hold the last frame. Only governs flipbook frame
    // selection; scroll/pulse/spin stay continuous either way.
    public static function setLoop(int entityId, bool loop): void {
        _native_billboard_setLoop(entityId, loop);
    }

    // Re-anchor the animation origin to the current engine time so the flipbook
    // (and the scroll/pulse/spin phase) restart from now. Needed for play-once
    // animations and pooled/reused markers.
    public static function restart(int entityId): void {
        _native_billboard_restart(entityId);
    }

    // True only for a one-shot (setLoop false) flipbook whose single cycle has
    // completed. Always false for looping or non-animated billboards.
    public static function isAnimationFinished(int entityId): bool {
        return _native_billboard_isAnimationFinished(entityId);
    }

    // ============================================
    // Visibility
    // ============================================

    // Show/hide the marker in Play mode. false hides it without removing the
    // component (toggles the editor-only gate).
    public static function setVisible(int entityId, bool visible): void {
        _native_billboard_setVisible(entityId, visible);
    }
}
