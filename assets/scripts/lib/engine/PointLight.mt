// PointLight - Static utility class for point light component operations
// Works with entity IDs (int) to control point light properties
//
// Usage examples:
//   int self = Entity::self();
//   PointLight::setColor(self, 1.0, 0.5, 0.0);    // Orange light
//   PointLight::setIntensity(self, 2.0);          // Set brightness
//   PointLight::setRadius(self, 15.0);            // Set light range
//   float radius = PointLight::getRadius(self);
//
// Note: Position is derived from the entity's WorldTransform, not stored in the component

public class PointLight {
    public constructor() {
    }

    // ============================================
    // Color Control
    // ============================================

    // Get the light color as [r, g, b] array
    // Returns null if entity doesn't have PointLightComponent
    public static function getColor(int entityId): float[] {
        return _native_pointLight_getColor(entityId);
    }

    // Set the light color (r, g, b values from 0.0 to 1.0)
    public static function setColor(int entityId, float r, float g, float b): void {
        _native_pointLight_setColor(entityId, r, g, b);
    }

    // ============================================
    // Intensity Control
    // ============================================

    // Get the light intensity
    // Returns 0.0 if entity doesn't have PointLightComponent
    public static function getIntensity(int entityId): float {
        return _native_pointLight_getIntensity(entityId);
    }

    // Set the light intensity (typically 0.0 to 10.0+)
    public static function setIntensity(int entityId, float intensity): void {
        _native_pointLight_setIntensity(entityId, intensity);
    }

    // ============================================
    // Radius Control
    // ============================================

    // Get the light radius (range of effect)
    // Returns 0.0 if entity doesn't have PointLightComponent
    public static function getRadius(int entityId): float {
        return _native_pointLight_getRadius(entityId);
    }

    // Set the light radius (range of effect in world units)
    public static function setRadius(int entityId, float radius): void {
        _native_pointLight_setRadius(entityId, radius);
    }
}
