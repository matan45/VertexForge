// Material - Static utility class for runtime material parameter overrides
// Works with entity IDs (int); the entity must have a MaterialComponent.
//
// Overrides target the material's NAMED parameters (nodes exposed with
// "Expose as parameter" in the Material Editor). Values apply per entity,
// on top of any material-instance overrides, and reset when play mode stops.
// Only parameters that fold into PBR outputs (albedo, metallic, roughness,
// AO, emission, opacity, IBL) are visible in the world view at runtime —
// the editor shows a [world] badge next to those.
//
// Usage examples:
//   int self = Entity::self();
//   Material::setColor(self, "Team Tint", 1.0, 0.2, 0.2, 1.0);  // red tint
//   Material::setScalar(self, "Damage Glow", 0.8);
//   Material::resetParameter(self, "Damage Glow");
//   Material::resetAllParameters(self);

import * from "../math/Vec2f.mt";
import * from "../math/Vec3f.mt";

public class Material {
    public constructor() {
    }

    // ============================================
    // Setters
    // ============================================

    // Override a scalar parameter
    public static function setScalar(int entityId, string name, float value): bool {
        return _native_material_setScalar(entityId, name, value);
    }

    // Override a vec2 parameter
    public static function setVec2(int entityId, string name, Vec2f value): bool {
        return _native_material_setVec2(entityId, name, value.x, value.y);
    }

    // Override a vec3 parameter
    public static function setVec3(int entityId, string name, Vec3f value): bool {
        return _native_material_setVec3(entityId, name, value.x, value.y, value.z);
    }

    // Override a color (vec4) parameter
    public static function setColor(int entityId, string name, float r, float g, float b, float a): bool {
        return _native_material_setColor(entityId, name, r, g, b, a);
    }

    // ============================================
    // Getters
    // ============================================

    // Get the current scalar override (0.0 when not overridden)
    public static function getScalar(int entityId, string name): float {
        return _native_material_getScalar(entityId, name);
    }

    // True when the entity currently overrides the parameter
    public static function hasParameter(int entityId, string name): bool {
        return _native_material_hasParameter(entityId, name);
    }

    // ============================================
    // Reset
    // ============================================

    // Remove one override (parameter falls back to instance/material value)
    public static function resetParameter(int entityId, string name): bool {
        return _native_material_resetParameter(entityId, name);
    }

    // Remove all overrides on the entity
    public static function resetAllParameters(int entityId): bool {
        return _native_material_resetAllParameters(entityId);
    }
}
