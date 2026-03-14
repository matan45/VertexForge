// Decal - Static utility class for Decal component operations
// Projects textures onto scene geometry (footprints, bullet holes, blood, etc.)
//
// Usage examples:
//   int self = Entity::self();
//   Decal::setColor(self, 1.0, 0.0, 0.0, 0.8);  // Red with 80% opacity
//   Decal::setHalfExtents(self, 2.0, 2.0, 0.5);  // 4x4 unit decal, 1 unit projection depth
//   Decal::setEdgeFalloff(self, 0.2);             // Soft edges
//   Decal::setSortPriority(self, 10);             // Render on top of priority < 10

public class Decal {
    public constructor() {
    }

    // ============================================
    // Color (RGBA tint and opacity)
    // ============================================

    // Get decal color as [r, g, b, a] array
    public static function getColor(int entityId): float[] {
        return _native_decal_getColor(entityId);
    }

    // Set decal color and opacity
    public static function setColor(int entityId, float r, float g, float b, float a): void {
        _native_decal_setColor(entityId, r, g, b, a);
    }

    // ============================================
    // Half Extents (projection volume size)
    // ============================================

    // Get half extents as [x, y, z] array
    // x=width, y=height, z=projection depth
    public static function getHalfExtents(int entityId): float[] {
        return _native_decal_getHalfExtents(entityId);
    }

    // Set half extents (controls decal volume size)
    // width and height control the decal surface area
    // depth controls how far the decal projects onto geometry
    public static function setHalfExtents(int entityId, float x, float y, float z): void {
        _native_decal_setHalfExtents(entityId, x, y, z);
    }

    // ============================================
    // Angle Fade (avoid stretching on steep surfaces)
    // ============================================

    // Get angle fade thresholds as [start, end] array
    // Values are cosine of angle (0=parallel, 1=perpendicular to surface)
    public static function getAngleFade(int entityId): float[] {
        return _native_decal_getAngleFade(entityId);
    }

    // Set angle fade thresholds
    // start: cosine angle above which decal is fully visible (default 0.7)
    // end: cosine angle below which decal is fully faded (default 0.3)
    public static function setAngleFade(int entityId, float start, float end): void {
        _native_decal_setAngleFade(entityId, start, end);
    }

    // ============================================
    // Edge Falloff
    // ============================================

    // Get edge falloff distance (0-1 range, fraction of OBB size)
    public static function getEdgeFalloff(int entityId): float {
        return _native_decal_getEdgeFalloff(entityId);
    }

    // Set edge falloff for soft edges at volume boundaries
    public static function setEdgeFalloff(int entityId, float falloff): void {
        _native_decal_setEdgeFalloff(entityId, falloff);
    }

    // ============================================
    // Sort Priority
    // ============================================

    // Get sort priority (higher = rendered on top)
    public static function getSortPriority(int entityId): int {
        return _native_decal_getSortPriority(entityId);
    }

    // Set sort priority for overlapping decals
    public static function setSortPriority(int entityId, int priority): void {
        _native_decal_setSortPriority(entityId, priority);
    }

    // ============================================
    // Normal Strength
    // ============================================

    // Get normal map influence strength
    public static function getNormalStrength(int entityId): float {
        return _native_decal_getNormalStrength(entityId);
    }

    // Set normal map influence (0=no effect, 1=full, >1=exaggerated)
    public static function setNormalStrength(int entityId, float strength): void {
        _native_decal_setNormalStrength(entityId, strength);
    }
}
