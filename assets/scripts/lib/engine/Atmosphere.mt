// Atmosphere - Controls the physical atmosphere rendering system
//
// Usage:
//   import * from "lib/engine/Atmosphere.mt";
//   Atmosphere.setEnabled(true);
//   Atmosphere.setSunElevation(30.0);
//   Atmosphere.setMieAnisotropy(0.76);

public class Atmosphere {
    public constructor() {
    }

    // ============================================
    // Global
    // ============================================

    public static function isEnabled(): bool {
        return _native_atmosphere_isEnabled();
    }

    public static function setEnabled(bool enabled): void {
        _native_atmosphere_setEnabled(enabled);
    }

    // ============================================
    // Planet
    // ============================================

    // Planet radius in meters (default: 6360000)
    public static function setPlanetRadius(float radius): void {
        _native_atmosphere_setPlanetRadius(radius);
    }

    // Atmosphere outer radius in meters (default: 6460000)
    public static function setAtmosphereRadius(float radius): void {
        _native_atmosphere_setAtmosphereRadius(radius);
    }

    // ============================================
    // Rayleigh Scattering (blue sky)
    // ============================================

    // RGB scattering coefficients (default: 5.802e-6, 13.558e-6, 33.1e-6)
    public static function setRayleighScattering(float r, float g, float b): void {
        _native_atmosphere_setRayleighScattering(r, g, b);
    }

    // ============================================
    // Mie Scattering (haze/glare)
    // ============================================

    // Mie scattering coefficient (default: 3.996e-6)
    public static function setMieScattering(float value): void {
        _native_atmosphere_setMieScattering(value);
    }

    // Mie anisotropy / directionality -1..1 (default: 0.8)
    public static function setMieAnisotropy(float g): void {
        _native_atmosphere_setMieAnisotropy(g);
    }

    // ============================================
    // Sun
    // ============================================

    // Sun spectral irradiance RGB (default: 1.474, 1.8504, 1.91198)
    public static function setSunIrradiance(float r, float g, float b): void {
        _native_atmosphere_setSunIrradiance(r, g, b);
    }

    public static function getSunIrradiance(): float[] {
        return _native_atmosphere_getSunIrradiance();
    }

    // Sun elevation angle in degrees -90..90 (default: 45)
    public static function setSunElevation(float degrees): void {
        _native_atmosphere_setSunElevation(degrees);
    }

    // Sun azimuth angle in degrees 0..360 (default: 0)
    public static function setSunAzimuth(float degrees): void {
        _native_atmosphere_setSunAzimuth(degrees);
    }

    // ============================================
    // Ground
    // ============================================

    // Ground albedo RGB 0..1 (default: 0.3, 0.3, 0.3)
    public static function setGroundAlbedo(float r, float g, float b): void {
        _native_atmosphere_setGroundAlbedo(r, g, b);
    }

    // ============================================
    // Aerial Perspective
    // ============================================

    // Max distance for aerial perspective in meters (default: 100000)
    public static function setAerialMaxDistance(float meters): void {
        _native_atmosphere_setAerialMaxDist(meters);
    }

    // Aerial perspective intensity 0..5 (default: 1.0)
    public static function setAerialIntensity(float intensity): void {
        _native_atmosphere_setAerialIntensity(intensity);
    }

    // ============================================
    // Day-Night Cycle
    // ============================================

    // Whether the automatic day-night cycle is active
    public static function isDayNightEnabled(): bool {
        return _native_atmosphere_isDayNightEnabled();
    }

    public static function setDayNightEnabled(bool enabled): void {
        _native_atmosphere_setDayNightEnabled(enabled);
    }

    // Current time of day in hours 0..24 (default: 12)
    public static function getTimeOfDay(): float {
        return _native_atmosphere_getTimeOfDay();
    }

    public static function setTimeOfDay(float hours): void {
        _native_atmosphere_setTimeOfDay(hours);
    }

    // Cycle speed: 1.0 = one game-hour per real minute (default: 1.0)
    public static function getCycleSpeed(): float {
        return _native_atmosphere_getCycleSpeed();
    }

    public static function setCycleSpeed(float speed): void {
        _native_atmosphere_setCycleSpeed(speed);
    }

    // ============================================
    // Moon
    // ============================================

    // Moon brightness relative to sun 0..1 (default: 0.03)
    public static function getMoonBrightness(): float {
        return _native_atmosphere_getMoonBrightness();
    }

    public static function setMoonBrightness(float brightness): void {
        _native_atmosphere_setMoonBrightness(brightness);
    }

    // ============================================
    // Stars
    // ============================================

    // Star density / probability per grid cell 0..0.05 (default: 0.006)
    public static function getStarDensity(): float {
        return _native_atmosphere_getStarDensity();
    }

    public static function setStarDensity(float density): void {
        _native_atmosphere_setStarDensity(density);
    }

    // Star HDR brightness multiplier 0..10 (default: 1.5)
    public static function getStarBrightness(): float {
        return _native_atmosphere_getStarBrightness();
    }

    public static function setStarBrightness(float brightness): void {
        _native_atmosphere_setStarBrightness(brightness);
    }

    // Night sky ambient brightness floor 0..0.05 (default: 0.002)
    public static function getNightSkyBrightness(): float {
        return _native_atmosphere_getNightSkyBrightness();
    }

    public static function setNightSkyBrightness(float brightness): void {
        _native_atmosphere_setNightSkyBrightness(brightness);
    }
}
