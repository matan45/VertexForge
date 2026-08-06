// Terrain - Static utility class for CPU terrain heightfield queries and runtime
// terrain editing (VK-1624).
//
// Reads the terrain's CPU height data directly (bilinear interpolated),
// independent of the physics collider. Reliable at runtime even when no
// terrain physics body exists.
//
// Editing model
// -------------
// Every edit is addressed in WORLD space. There is no terrain entity id and no
// "current sculpt target": the terrain whose tiles contain the edit CENTRE is
// the one that changes.
//
// Heights are WORLD Y units, not a normalised 0..1 heightmap. deform() with
// MODE_ADD adds a delta; MODE_SET drives toward an absolute height. Both take
// effect immediately - the very next heightAt() sees them - so a salvo can drop
// each shell into the crater the last one dug.
//
// The visible mesh catches up when the tile is next regenerated. Height-only
// edits reuse the existing meshlet topology; hole edits rebuild it.
//
// Batching
// --------
// What IS deferred is the expensive half: welding the seams between edited tiles
// and rebuilding their physics colliders. Wrap a burst so it is paid once:
//
//   Terrain::beginBatch();
//   Terrain::deform(x, z, 9.0,  0.4);   // ejecta lip  (wide, gentle, FIRST)
//   Terrain::deform(x, z, 6.0, -1.8);   // crater bowl (narrow, deep, SECOND)
//   Terrain::paint(x, z, 7.0, SCORCH_LAYER, 1.0);
//   Terrain::flush();
//
// Forgetting flush() is not fatal: the engine drains anything still pending a
// couple of frames later and logs a warning. Skipping beginBatch() entirely is
// fine too - edits then coalesce per frame, which is already one rebuild per
// tile per frame.
//
// paint() never needs a flush: weight maps have no seams and no collider.
//
// Limits
// ------
// Edits are TRANSIENT. They are never written back to the .vfTerrain asset, and
// a tile that streams out and back in reloads the authored heights - so a crater
// far outside the streaming radius will heal. Only tiles currently resident in
// memory can be edited; an edit that lands on one that is not simply reports
// fewer tiles touched.
//
// Rate limit: 64 edits per frame, brush radius clamped to 512 world units. Over
// the limit, calls return 0 and log a warning.
//
// Usage:
//   float y = Terrain::heightAt(worldX, worldZ);
//   if (Terrain::hasHeightAt(x, z)) { ... }
//   Terrain::deform(x, z, 6.0, -1.8);

public class Terrain {
    // Height edit modes.
    public static final int MODE_ADD = 0;   // h = h + amount * influence     (delta)
    public static final int MODE_SET = 1;   // h = mix(h, amount, influence)  (absolute)

    // Brush falloff curves, matching the sculpt tool.
    public static final int FALLOFF_CONSTANT = 0;
    public static final int FALLOFF_LINEAR   = 1;
    public static final int FALLOFF_SMOOTH   = 2;
    public static final int FALLOFF_SHARP    = 3;

    // Brush footprint.
    public static final int SHAPE_CIRCLE = 0;
    public static final int SHAPE_SQUARE = 1;

    // Layer paint modes. There is deliberately no "set base layer" mode exposed
    // to scripts: it wipes every weight channel of every touched tile.
    public static final int PAINT_ADD    = 0;
    public static final int PAINT_ERASE  = 1;
    public static final int PAINT_SMOOTH = 2;
    public static final int PAINT_FILL   = 3;

    public constructor() {
    }

    // ============================================
    // Queries
    // ============================================

    // World-space terrain height at (worldX, worldZ).
    // Returns 0.0 if the position is off the loaded terrain.
    public static function heightAt(float worldX, float worldZ): float {
        float[] r = _native_terrain_getHeightAt(worldX, worldZ);
        return r[1];
    }

    // True if (worldX, worldZ) lies over loaded terrain with valid height data.
    public static function hasHeightAt(float worldX, float worldZ): bool {
        float[] r = _native_terrain_getHeightAt(worldX, worldZ);
        return r[0] > 0.5;
    }

    // ============================================
    // Height editing
    // ============================================

    // Radial height edit with a smooth circular falloff. `amount` is a world-Y
    // delta at the brush centre, tapering to zero at the rim. Negative digs.
    // Returns the number of tiles changed; 0 means nothing happened.
    public static function deform(float centerX, float centerZ, float radius, float amount): int {
        return _native_terrain_deform(centerX, centerZ, radius, amount,
                                      Terrain::MODE_ADD, Terrain::FALLOFF_SMOOTH,
                                      Terrain::SHAPE_CIRCLE);
    }

    public static function deform(float centerX, float centerZ, float radius, float amount,
                                  int mode, int falloff, int shape): int {
        return _native_terrain_deform(centerX, centerZ, radius, amount, mode, falloff, shape);
    }

    // Drive the surface toward an absolute world Y inside the brush. With a
    // sharp square falloff this is the building-pad leveller.
    public static function flatten(float centerX, float centerZ, float radius, float targetY): int {
        return _native_terrain_deform(centerX, centerZ, radius, targetY,
                                      Terrain::MODE_SET, Terrain::FALLOFF_SMOOTH,
                                      Terrain::SHAPE_CIRCLE);
    }

    // ============================================
    // Layer weight painting
    // ============================================

    // Paint palette layer `layerIndex` (0-31) with a smooth circular falloff.
    // `strength` is the absolute influence at the brush centre, 0..1 - not a
    // per-second rate, so a single call paints what it says it paints.
    //
    // A tile whose eight weight channels are ALL taken by other layers is
    // skipped rather than having one evicted, because eviction zeroes that
    // channel and renormalises the whole tile. Returns tiles changed.
    public static function paint(float centerX, float centerZ, float radius,
                                 int layerIndex, float strength): int {
        return _native_terrain_paint(centerX, centerZ, radius, layerIndex, strength, 1.0,
                                     Terrain::FALLOFF_SMOOTH, Terrain::SHAPE_CIRCLE,
                                     Terrain::PAINT_ADD);
    }

    public static function paint(float centerX, float centerZ, float radius, int layerIndex,
                                 float strength, float opacity, int falloff, int shape,
                                 int paintMode): int {
        return _native_terrain_paint(centerX, centerZ, radius, layerIndex, strength,
                                     opacity, falloff, shape, paintMode);
    }

    // ============================================
    // Holes
    // ============================================

    // Punch holes through the terrain surface inside the brush. Holes are
    // per-QUAD, so the edge snaps to the quad grid, and the radius is exact -
    // there is no falloff to soften it. Returns tiles changed.
    public static function punchHole(float centerX, float centerZ, float radius): int {
        return _native_terrain_setHole(centerX, centerZ, radius, true, Terrain::SHAPE_CIRCLE);
    }

    public static function punchHole(float centerX, float centerZ, float radius, int shape): int {
        return _native_terrain_setHole(centerX, centerZ, radius, true, shape);
    }

    // Fill holes back in inside the brush.
    public static function fillHole(float centerX, float centerZ, float radius): int {
        return _native_terrain_setHole(centerX, centerZ, radius, false, Terrain::SHAPE_CIRCLE);
    }

    public static function fillHole(float centerX, float centerZ, float radius, int shape): int {
        return _native_terrain_setHole(centerX, centerZ, radius, false, shape);
    }

    // ============================================
    // Batching
    // ============================================

    // Hold the seam weld and the collider rebuild until the matching flush(), so
    // a burst of edits pays for them once instead of once each.
    public static function beginBatch(): void {
        _native_terrain_beginBatch();
    }

    // Close the batch. Returns the number of pending tile entries handed to the
    // engine; they are welded and their colliders rebuilt on the next terrain
    // tick. 0 means there was nothing to do.
    public static function flush(): int {
        return _native_terrain_flush();
    }
}
