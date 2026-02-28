// Water - Static utility class for water system operations
// Provides queries, property access, and commands for water bodies
//
// Usage examples:
//   bool inWater = Water::isInWater(new Vec3f(x, y, z));
//   float height = Water::getHeightAt(x, z);
//   float density = Water::getDensity(waterEntityId);
//   Water::setTileHeight(waterEntityId, tileX, tileZ, 5.0);

import * from "../math/Vec3f.mt";

public class Water {
    public constructor() {
    }

    // ============================================
    // Spatial Queries
    // ============================================

    // Check if a world position is inside any water volume
    public static function isInWater(Vec3f position): bool {
        return _native_water_isInWater(position.x, position.y, position.z);
    }

    // Get the water surface height at a world XZ position
    // Returns 0.0 if no water exists at that position
    public static function getHeightAt(float x, float z): float {
        return _native_water_getHeightAt(x, z);
    }

    // ============================================
    // Component Queries
    // ============================================

    // Check if entity has a WaterComponent (root water entity)
    public static function hasWater(int entityId): bool {
        return _native_water_hasWater(entityId);
    }

    // Check if entity has a WaterTileComponent (individual tile)
    public static function hasWaterTile(int entityId): bool {
        return _native_water_hasWaterTile(entityId);
    }

    // ============================================
    // Property Getters
    // ============================================

    // Get water height for entity (works with both WaterComponent and WaterTileComponent)
    public static function getWaterHeight(int entityId): float {
        return _native_water_getWaterHeight(entityId);
    }

    // Get wave speed (WaterComponent only)
    public static function getWaveSpeed(int entityId): float {
        return _native_water_getWaveSpeed(entityId);
    }

    // Get wave amplitude (WaterComponent only)
    public static function getWaveAmplitude(int entityId): float {
        return _native_water_getWaveAmplitude(entityId);
    }

    // Get water density in kg/m3 (WaterComponent only)
    public static function getDensity(int entityId): float {
        return _native_water_getDensity(entityId);
    }

    // Get drag coefficient (WaterComponent only)
    public static function getDrag(int entityId): float {
        return _native_water_getDrag(entityId);
    }

    // Get buoyancy strength multiplier (WaterComponent only)
    public static function getBuoyancyStrength(int entityId): float {
        return _native_water_getBuoyancyStrength(entityId);
    }

    // ============================================
    // Commands
    // ============================================

    // Set height for a specific water tile
    public static function setTileHeight(int waterEntityId, int tileX, int tileZ, float height): void {
        _native_water_setTileHeight(waterEntityId, tileX, tileZ, height);
    }

    // Set all global water settings at once
    public static function setGlobalSettings(int waterEntityId, float density, float drag,
                                              float buoyancyStrength, float waveSpeed,
                                              float waveAmplitude, float waveFrequency): void {
        _native_water_setGlobalSettings(waterEntityId, density, drag, buoyancyStrength,
                                         waveSpeed, waveAmplitude, waveFrequency);
    }
}
