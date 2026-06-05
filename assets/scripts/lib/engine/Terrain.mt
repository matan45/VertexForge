// Terrain - Static utility class for CPU terrain heightfield queries.
//
// Reads the terrain's CPU height data directly (bilinear interpolated),
// independent of the physics collider. Reliable at runtime even when no
// terrain physics body exists.
//
// Usage:
//   float y = Terrain::heightAt(worldX, worldZ);
//   if (Terrain::hasHeightAt(x, z)) { ... }

public class Terrain {
    public constructor() {
    }

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
}
