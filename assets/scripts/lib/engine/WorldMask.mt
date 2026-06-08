// WorldMask - CPU readback of the renderer's world-space XZ mask (e.g. fog of war).
//
// Samples the same data the terrain/entity shaders see. Returns 1.0 when no mask
// is bound, the mask is disabled, or the point is outside the mask bounds — the
// "no effect" cases — so gameplay checks become no-ops when the mask is off.
//
// The meaning of the value is owned by whoever binds the mask. For the RTS fog:
//   0.0 = unexplored, ~0.4 = explored but not visible, 1.0 = currently visible.
//
// Usage:
//   if (WorldMask::sample(x, z) >= 0.9) { /* currently visible */ }

public class WorldMask {
    public constructor() {
    }

    // Mask red channel at (worldX, worldZ), in [0,1].
    // 1.0 = no mask bound / mask disabled / outside mask bounds.
    public static function sample(float worldX, float worldZ): float {
        return _native_worldMask_sample(worldX, worldZ);
    }
}
