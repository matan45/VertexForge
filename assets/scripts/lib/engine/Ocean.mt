// Ocean - Static utility class for the scene's water: the ocean and its lakes / pools
// (water bodies). The spatial queries cover ANY water; the property getters and the
// commands that take an oceanEntityId act on the ocean entity, created as "Ocean":
// Entity::findByName("Ocean") finds it unless it was renamed (-1 without an ocean).
//
// A hidden ocean (ocean_enable {enabled:false}) keeps its settings, which still
// style and tune every lake and pool, but has no surface: the queries below answer
// only for the lakes and pools then.
//
// Usage examples:
//   bool inWater = Ocean::isInOcean(new Vec3f(x, y, z));
//   float height = Ocean::getOceanHeightAt(x, z);
//   int ocean = Entity::findByName("Ocean");
//   float drag = Ocean::getDrag(ocean);
//   Ocean::setPhysicsSettings(ocean, Ocean::getDensity(ocean), drag, buoyancyStrength);

import * from "../math/Vec3f.mt";

public class Ocean {
    public constructor() {
    }

    // ============================================
    // Spatial Queries
    // ============================================

    // Check if a world position is under a water surface: a lake or pool's (inside its
    // rectangle and above its floor), else the visible ocean's (waves included).
    // False where there is no water.
    public static function isInOcean(Vec3f position): bool {
        return _native_ocean_isInOcean(position.x, position.y, position.z);
    }

    // Get the water surface height at a world XZ position: a lake or pool's level inside
    // its rectangle, else the visible ocean's surface (waves included).
    // Returns 0.0 where there is no water - check isInOcean when y = 0 is a real answer.
    public static function getOceanHeightAt(float x, float z): float {
        return _native_ocean_getOceanHeightAt(x, z);
    }

    // Get the water depth (surface down to the terrain) at a world XZ position: inside a
    // lake or pool at most its depth; over the visible ocean a very large value where there
    // is no terrain below (open sea). Returns 0.0 where there is no water.
    public static function getWaterDepthAt(float x, float z): float {
        return _native_ocean_getWaterDepthAt(x, z);
    }

    // Check if the primary camera is currently under any water surface (lake, pool or the
    // visible ocean)
    public static function isCameraUnderwater(): bool {
        return _native_ocean_isCameraUnderwater();
    }

    // Check if a dynamic rigid body is currently (partially) submerged in water that floats
    // things (a lake or pool, or the visible ocean, whose physics is on). Updated by the
    // physics step, so only in Play mode.
    public static function isEntityInWater(int entityId): bool {
        return _native_ocean_isEntityInWater(entityId);
    }

    // ============================================
    // Component Queries
    // ============================================

    // Check if entity has an OceanComponent (true for a hidden ocean too)
    public static function hasOcean(int entityId): bool {
        return _native_ocean_hasOcean(entityId);
    }

    // ============================================
    // Property Getters
    // ============================================

    // Get the ocean's level (base water height, before waves), also while it is hidden
    public static function getBaseHeight(int entityId): float {
        return _native_ocean_getBaseHeight(entityId);
    }

    // Get ocean density in kg/m3 (stored only: buoyancy does not use it)
    public static function getDensity(int entityId): float {
        return _native_ocean_getDensity(entityId);
    }

    // Get drag coefficient (applies to bodies floating in ANY water, lakes and pools included)
    public static function getDrag(int entityId): float {
        return _native_ocean_getDrag(entityId);
    }

    // Get buoyancy strength multiplier (applies to ANY water, lakes and pools included)
    public static function getBuoyancyStrength(int entityId): float {
        return _native_ocean_getBuoyancyStrength(entityId);
    }

    // ============================================
    // Commands
    // ============================================

    // Set the ocean's buoyancy tuning: drag and buoyancyStrength apply to bodies floating in ANY
    // water, lakes and pools included; density is stored but unused. Keeps the ocean's physics
    // switch (whether the sea itself floats things) as it is.
    public static function setPhysicsSettings(int oceanEntityId, float density, float drag,
                                               float buoyancyStrength): void {
        _native_ocean_setPhysicsSettings(oceanEntityId, density, drag, buoyancyStrength);
    }

    // Set the sea state by Beaufort number (0 = mirror calm .. 12 = hurricane).
    // transitionSeconds > 0 ramps the waves smoothly over that duration. While the ocean is
    // weather-driven and the weather is enabled, the weather takes over again on the next frame
    // (after the transition, if any).
    public static function setSeaState(float beaufort, float transitionSeconds): void {
        _native_ocean_setSeaState(beaufort, transitionSeconds);
    }

    // Get the current sea state as a Beaufort number (0.0 without an ocean)
    public static function getSeaState(): float {
        return _native_ocean_getSeaState();
    }

    // Enable/disable weather-driven sea state (ocean follows WeatherState wind/gusts while the
    // weather is enabled). Keeps the ocean's current weather response.
    public static function setWeatherDriven(int oceanEntityId, bool enabled): void {
        _native_ocean_setWeatherDriven(oceanEntityId, enabled);
    }

    // Push one disturbance into the interactive ripple patch: a splash, a projectile hit, an oar
    // stroke. radius is in metres and the ripple is exactly zero outside it; strength is the
    // vertical velocity kick at the centre (negative pushes the surface down, which is what an
    // impact does). Impulses outside the camera-following patch are silently dropped, and so is
    // every impulse while the ripple simulation is off.
    public static function addImpulse(float x, float z, float radius, float strength): void {
        _native_ocean_addImpulse(x, z, radius, strength);
    }

    // Turn the interactive ripple simulation on or off at runtime. It is an ocean setting that
    // also ripples every lake and pool; without an ocean entity this does nothing.
    public static function setRippleSimEnabled(bool enabled): void {
        _native_ocean_setRippleEnabled(enabled);
    }

    public static function isRippleSimEnabled(): bool {
        return _native_ocean_isRippleEnabled();
    }

    // Set the water's colours and fresnel power. These are the ocean's settings and every lake
    // and pool uses them too. shallowA is the surface opacity; deepA and maxVisibleDepth are
    // stored but unused.
    public static function setVisualSettings(int oceanEntityId,
                                              float shallowR, float shallowG, float shallowB, float shallowA,
                                              float deepR, float deepG, float deepB, float deepA,
                                              float maxVisibleDepth, float fresnelPower): void {
        _native_ocean_setVisualSettings(oceanEntityId, shallowR, shallowG, shallowB, shallowA,
                                         deepR, deepG, deepB, deepA, maxVisibleDepth, fresnelPower);
    }
}
