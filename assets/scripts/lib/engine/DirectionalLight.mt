// DirectionalLight - Static utility class for directional light component operations
// Works with entity IDs (int) to control directional light properties
//
// Usage examples:
//   int self = Entity::self();
//   DirectionalLight::setColor(self, new Vec3f(1.0, 0.9, 0.8));  // Warm sunlight color
//   DirectionalLight::setIntensity(self, 1.5);                   // Increase brightness
//   Vec3f color = DirectionalLight::getColor(self);
//
// Note: Direction is derived from the entity's Transform rotation, not stored in the component

import * from "../math/Vec3f.mt";

public class DirectionalLight {
    public constructor() {
    }

    // ============================================
    // Color Control
    // ============================================

    // Get the light color as Vec3f (r, g, b)
    public static function getColor(int entityId): Vec3f {
        float[] c = _native_directionalLight_getColor(entityId);
        return new Vec3f(c[0], c[1], c[2]);
    }

    // Set the light color (r, g, b values from 0.0 to 1.0)
    public static function setColor(int entityId, Vec3f color): void {
        _native_directionalLight_setColor(entityId, color.x, color.y, color.z);
    }

    // ============================================
    // Intensity Control
    // ============================================

    // Get the light intensity
    // Returns 0.0 if entity doesn't have DirectionalLightComponent
    public static function getIntensity(int entityId): float {
        return _native_directionalLight_getIntensity(entityId);
    }

    // Set the light intensity (typically 0.0 to 10.0+)
    public static function setIntensity(int entityId, float intensity): void {
        _native_directionalLight_setIntensity(entityId, intensity);
    }
}
