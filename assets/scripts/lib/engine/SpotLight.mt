// SpotLight - Static utility class for spot light component operations
// Works with entity IDs (int) to control spot light properties
//
// Usage examples:
//   int self = Entity::self();
//   SpotLight::setColor(self, 1.0, 1.0, 1.0);      // White light
//   SpotLight::setIntensity(self, 5.0);            // Set brightness
//   SpotLight::setInnerAngle(self, 25.0);          // Inner cone angle
//   SpotLight::setOuterAngle(self, 35.0);          // Outer cone angle
//   SpotLight::setRange(self, 30.0);               // Light distance
//
// Note: Position and direction are derived from the entity's Transform, not stored in the component

public class SpotLight {
    public constructor() {
    }

    // ============================================
    // Color Control
    // ============================================

    // Get the light color as [r, g, b] array
    // Returns null if entity doesn't have SpotLightComponent
    public static function getColor(int entityId): float[] {
        return _native_spotLight_getColor(entityId);
    }

    // Set the light color (r, g, b values from 0.0 to 1.0)
    public static function setColor(int entityId, float r, float g, float b): void {
        _native_spotLight_setColor(entityId, r, g, b);
    }

    // ============================================
    // Intensity Control
    // ============================================

    // Get the light intensity
    // Returns 0.0 if entity doesn't have SpotLightComponent
    public static function getIntensity(int entityId): float {
        return _native_spotLight_getIntensity(entityId);
    }

    // Set the light intensity (typically 0.0 to 10.0+)
    public static function setIntensity(int entityId, float intensity): void {
        _native_spotLight_setIntensity(entityId, intensity);
    }

    // ============================================
    // Cone Angle Control
    // ============================================

    // Get the inner cone angle in degrees
    // Returns 0.0 if entity doesn't have SpotLightComponent
    public static function getInnerAngle(int entityId): float {
        return _native_spotLight_getInnerAngle(entityId);
    }

    // Set the inner cone angle in degrees (area of full brightness)
    public static function setInnerAngle(int entityId, float angle): void {
        _native_spotLight_setInnerAngle(entityId, angle);
    }

    // Get the outer cone angle in degrees
    // Returns 0.0 if entity doesn't have SpotLightComponent
    public static function getOuterAngle(int entityId): float {
        return _native_spotLight_getOuterAngle(entityId);
    }

    // Set the outer cone angle in degrees (edge of light falloff)
    // Should be greater than inner angle
    public static function setOuterAngle(int entityId, float angle): void {
        _native_spotLight_setOuterAngle(entityId, angle);
    }

    // ============================================
    // Range Control
    // ============================================

    // Get the light range (maximum distance)
    // Returns 0.0 if entity doesn't have SpotLightComponent
    public static function getRange(int entityId): float {
        return _native_spotLight_getRange(entityId);
    }

    // Set the light range (maximum distance in world units)
    public static function setRange(int entityId, float range): void {
        _native_spotLight_setRange(entityId, range);
    }
}
