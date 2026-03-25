// Cloud - Controls the volumetric cloud rendering system
//
// Usage:
//   import * from "lib/engine/Cloud.mt";
//   Cloud.setEnabled(true);
//   Cloud.setCoverage(0.7);
//   Cloud.setDensity(0.8);
//   Cloud.setWindSpeed(30.0);

public class Cloud {
    public constructor() {
    }

    // ============================================
    // Global
    // ============================================

    public static function isEnabled(): bool {
        return _native_cloud_isEnabled();
    }

    public static function setEnabled(bool enabled): void {
        _native_cloud_setEnabled(enabled);
    }

    // ============================================
    // Color
    // ============================================

    // Tint the cloud color RGB (default: 1,1,1 = white)
    // Use warm tones for sunset, grey for overcast
    public static function setColorTint(float r, float g, float b): void {
        _native_cloud_setColorTint(r, g, b);
    }

    public static function getColorTint(): float[] {
        return _native_cloud_getColorTint();
    }

    // ============================================
    // Cloud Layer (altitude in meters)
    // ============================================

    // Bottom of cloud layer in meters (default: 1500)
    public static function setMinAltitude(float meters): void {
        _native_cloud_setMinAltitude(meters);
    }

    // Top of cloud layer in meters (default: 4000)
    public static function setMaxAltitude(float meters): void {
        _native_cloud_setMaxAltitude(meters);
    }

    // ============================================
    // Density & Coverage
    // ============================================

    // Global density multiplier 0..1 (default: 0.8)
    public static function setDensity(float value): void {
        _native_cloud_setDensity(value);
    }

    public static function getDensity(): float {
        return _native_cloud_getDensity();
    }

    // Coverage: amount of sky covered by clouds 0..1 (default: 0.6)
    // 0 = clear sky, 1 = overcast
    public static function setCoverage(float value): void {
        _native_cloud_setCoverage(value);
    }

    public static function getCoverage(): float {
        return _native_cloud_getCoverage();
    }

    // Cloud type 0..1 (default: 0.5)
    // 0 = flat stratus, 1 = tall cumulus
    public static function setCloudType(float value): void {
        _native_cloud_setCloudType(value);
    }

    // ============================================
    // Noise Shaping
    // ============================================

    // Shape noise scale (default: 0.0003)
    public static function setShapeScale(float value): void {
        _native_cloud_setShapeScale(value);
    }

    // Detail noise scale (default: 0.003)
    public static function setDetailScale(float value): void {
        _native_cloud_setDetailScale(value);
    }

    // Erosion strength 0..1 (default: 0.3)
    public static function setErosionStrength(float value): void {
        _native_cloud_setErosionStrength(value);
    }

    // ============================================
    // Wind
    // ============================================

    // Wind speed in m/s (default: 25)
    public static function setWindSpeed(float metersPerSecond): void {
        _native_cloud_setWindSpeed(metersPerSecond);
    }

    // Wind direction in degrees 0..360 (default: 45)
    public static function setWindDirection(float degrees): void {
        _native_cloud_setWindDirection(degrees);
    }

    // ============================================
    // Lighting
    // ============================================

    // Light absorption coefficient 0..2 (default: 0.75)
    public static function setLightAbsorption(float value): void {
        _native_cloud_setLightAbsorption(value);
    }

    // Ambient sky light intensity 0..2 (default: 0.8)
    public static function setAmbientIntensity(float value): void {
        _native_cloud_setAmbientIntensity(value);
    }

    // Silver lining intensity 0..2 (default: 0.5)
    // Bright rim on cloud edges when looking toward the sun
    public static function setSilverLiningIntensity(float value): void {
        _native_cloud_setSilverLiningIntensity(value);
    }

    // Silver lining angular spread 1..20 (default: 5)
    // Higher = narrower highlight toward the sun
    public static function setSilverLiningSpread(float value): void {
        _native_cloud_setSilverLiningSpread(value);
    }

    // Multi-scatter interior boost 0..2 (default: 0.5)
    // Brightens thick cloud interiors to prevent unnaturally dark centers
    public static function setMultiScatterBoost(float value): void {
        _native_cloud_setMultiScatterBoost(value);
    }

    // ============================================
    // Performance
    // ============================================

    // Temporal reprojection blend factor 0..1 (default: 0.8)
    // Higher = smoother but more ghosting
    public static function setTemporalBlend(float value): void {
        _native_cloud_setTemporalBlend(value);
    }

    // Max ray march steps 16..256 (default: 96)
    public static function setMaxMarchSteps(int steps): void {
        _native_cloud_setMaxMarchSteps(steps);
    }
}
