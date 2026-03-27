// Ocean - Static utility class for ocean system operations
// Provides queries, property access, and commands for ocean bodies
//
// Usage examples:
//   bool inOcean = Ocean::isInOcean(new Vec3f(x, y, z));
//   float height = Ocean::getOceanHeightAt(x, z);
//   float density = Ocean::getDensity(oceanEntityId);
//   Ocean::setPhysicsSettings(oceanEntityId, density, drag, buoyancyStrength);

import * from "../math/Vec3f.mt";

public class Ocean {
    public constructor() {
    }

    // ============================================
    // Spatial Queries
    // ============================================

    // Check if a world position is inside the ocean volume
    public static function isInOcean(Vec3f position): bool {
        return _native_ocean_isInOcean(position.x, position.y, position.z);
    }

    // Get the ocean surface height at a world XZ position
    // Returns 0.0 if no ocean exists at that position
    public static function getOceanHeightAt(float x, float z): float {
        return _native_ocean_getOceanHeightAt(x, z);
    }

    // Check if the camera is currently underwater
    public static function isCameraUnderwater(): bool {
        return _native_ocean_isCameraUnderwater();
    }

    // ============================================
    // Component Queries
    // ============================================

    // Check if entity has an OceanComponent
    public static function hasOcean(int entityId): bool {
        return _native_ocean_hasOcean(entityId);
    }

    // ============================================
    // Property Getters
    // ============================================

    // Get base water height for ocean entity
    public static function getBaseHeight(int entityId): float {
        return _native_ocean_getBaseHeight(entityId);
    }

    // Get ocean density in kg/m3
    public static function getDensity(int entityId): float {
        return _native_ocean_getDensity(entityId);
    }

    // Get drag coefficient
    public static function getDrag(int entityId): float {
        return _native_ocean_getDrag(entityId);
    }

    // Get buoyancy strength multiplier
    public static function getBuoyancyStrength(int entityId): float {
        return _native_ocean_getBuoyancyStrength(entityId);
    }

    // ============================================
    // Commands
    // ============================================

    // Set ocean physics settings
    public static function setPhysicsSettings(int oceanEntityId, float density, float drag,
                                               float buoyancyStrength): void {
        _native_ocean_setPhysicsSettings(oceanEntityId, density, drag, buoyancyStrength);
    }

    // Set ocean visual settings
    public static function setVisualSettings(int oceanEntityId,
                                              float shallowR, float shallowG, float shallowB, float shallowA,
                                              float deepR, float deepG, float deepB, float deepA,
                                              float maxVisibleDepth, float fresnelPower): void {
        _native_ocean_setVisualSettings(oceanEntityId, shallowR, shallowG, shallowB, shallowA,
                                         deepR, deepG, deepB, deepA, maxVisibleDepth, fresnelPower);
    }
}
