// PostProcess - Static utility class for post-processing settings
// Controls all post-process effects: tone mapping, FXAA, bloom, vignette,
// chromatic aberration, and film grain.
//
// Usage examples:
//   PostProcess::setEnabled(true);
//   PostProcess::setToneMappingMode(ToneMappingMode::ACES);
//   PostProcess::setExposure(1.5);
//   PostProcess::setBloomEnabled(true);
//   PostProcess::setBloomIntensity(0.8);

public class PostProcess {
    public constructor() {
    }

    // ============================================
    // Global
    // ============================================

    public static function isEnabled(): bool {
        return _native_postprocess_isEnabled();
    }

    public static function setEnabled(bool enabled): void {
        _native_postprocess_setEnabled(enabled);
    }

    // ============================================
    // Tone Mapping
    // ============================================

    public static function isToneMappingEnabled(): bool {
        return _native_postprocess_toneMapping_isEnabled();
    }

    public static function setToneMappingEnabled(bool enabled): void {
        _native_postprocess_toneMapping_setEnabled(enabled);
    }

    // Get current tone mapping mode (see ToneMappingMode constants)
    public static function getToneMappingMode(): int {
        return _native_postprocess_toneMapping_getMode();
    }

    // Set tone mapping mode (use ToneMappingMode constants)
    public static function setToneMappingMode(int mode): void {
        _native_postprocess_toneMapping_setMode(mode);
    }

    // Exposure: controls scene brightness before tone mapping (0.01 - 10.0)
    public static function getExposure(): float {
        return _native_postprocess_toneMapping_getExposure();
    }

    public static function setExposure(float value): void {
        _native_postprocess_toneMapping_setExposure(value);
    }

    // Gamma: display gamma correction, 2.2 is standard (1.0 - 3.0)
    public static function getGamma(): float {
        return _native_postprocess_toneMapping_getGamma();
    }

    public static function setGamma(float value): void {
        _native_postprocess_toneMapping_setGamma(value);
    }

    // Contrast: adjusts contrast around mid-gray (0.5 - 2.0)
    public static function getContrast(): float {
        return _native_postprocess_toneMapping_getContrast();
    }

    public static function setContrast(float value): void {
        _native_postprocess_toneMapping_setContrast(value);
    }

    // ============================================
    // FXAA
    // ============================================

    public static function isFXAAEnabled(): bool {
        return _native_postprocess_fxaa_isEnabled();
    }

    public static function setFXAAEnabled(bool enabled): void {
        _native_postprocess_fxaa_setEnabled(enabled);
    }

    // Get FXAA quality level (see FXAAQuality constants)
    public static function getFXAAQuality(): int {
        return _native_postprocess_fxaa_getQuality();
    }

    // Set FXAA quality level (use FXAAQuality constants)
    public static function setFXAAQuality(int quality): void {
        _native_postprocess_fxaa_setQuality(quality);
    }

    // Minimum luminance threshold for edge detection (0.01 - 0.1)
    public static function getEdgeThresholdMin(): float {
        return _native_postprocess_fxaa_getEdgeThresholdMin();
    }

    public static function setEdgeThresholdMin(float value): void {
        _native_postprocess_fxaa_setEdgeThresholdMin(value);
    }

    // Maximum luminance threshold for edge detection (0.05 - 0.5)
    public static function getEdgeThreshold(): float {
        return _native_postprocess_fxaa_getEdgeThreshold();
    }

    public static function setEdgeThreshold(float value): void {
        _native_postprocess_fxaa_setEdgeThreshold(value);
    }

    // ============================================
    // Bloom
    // ============================================

    public static function isBloomEnabled(): bool {
        return _native_postprocess_bloom_isEnabled();
    }

    public static function setBloomEnabled(bool enabled): void {
        _native_postprocess_bloom_setEnabled(enabled);
    }

    // Brightness threshold for bloom extraction (0.0 - 1.0)
    public static function getBloomThreshold(): float {
        return _native_postprocess_bloom_getThreshold();
    }

    public static function setBloomThreshold(float value): void {
        _native_postprocess_bloom_setThreshold(value);
    }

    // Bloom intensity (0.0 - 2.0)
    public static function getBloomIntensity(): float {
        return _native_postprocess_bloom_getIntensity();
    }

    public static function setBloomIntensity(float value): void {
        _native_postprocess_bloom_setIntensity(value);
    }

    // Bloom spread radius (0.0 - 2.0)
    public static function getBloomRadius(): float {
        return _native_postprocess_bloom_getRadius();
    }

    public static function setBloomRadius(float value): void {
        _native_postprocess_bloom_setRadius(value);
    }

    // Number of blur passes (1 - 10)
    public static function getBloomPasses(): int {
        return _native_postprocess_bloom_getPasses();
    }

    public static function setBloomPasses(int value): void {
        _native_postprocess_bloom_setPasses(value);
    }

    // ============================================
    // Vignette
    // ============================================

    public static function isVignetteEnabled(): bool {
        return _native_postprocess_vignette_isEnabled();
    }

    public static function setVignetteEnabled(bool enabled): void {
        _native_postprocess_vignette_setEnabled(enabled);
    }

    // Vignette darkness intensity (0.0 - 1.0)
    public static function getVignetteIntensity(): float {
        return _native_postprocess_vignette_getIntensity();
    }

    public static function setVignetteIntensity(float value): void {
        _native_postprocess_vignette_setIntensity(value);
    }

    // Vignette start radius from center (0.0 - 1.5)
    public static function getVignetteRadius(): float {
        return _native_postprocess_vignette_getRadius();
    }

    public static function setVignetteRadius(float value): void {
        _native_postprocess_vignette_setRadius(value);
    }

    // Vignette falloff softness (0.0 - 1.0)
    public static function getVignetteSoftness(): float {
        return _native_postprocess_vignette_getSoftness();
    }

    public static function setVignetteSoftness(float value): void {
        _native_postprocess_vignette_setSoftness(value);
    }

    // ============================================
    // Chromatic Aberration
    // ============================================

    public static function isChromaticAberrationEnabled(): bool {
        return _native_postprocess_chromaticAberration_isEnabled();
    }

    public static function setChromaticAberrationEnabled(bool enabled): void {
        _native_postprocess_chromaticAberration_setEnabled(enabled);
    }

    // Color channel separation amount (0.0 - 0.05)
    public static function getChromaticAberrationIntensity(): float {
        return _native_postprocess_chromaticAberration_getIntensity();
    }

    public static function setChromaticAberrationIntensity(float value): void {
        _native_postprocess_chromaticAberration_setIntensity(value);
    }

    // ============================================
    // Film Grain
    // ============================================

    public static function isFilmGrainEnabled(): bool {
        return _native_postprocess_filmGrain_isEnabled();
    }

    public static function setFilmGrainEnabled(bool enabled): void {
        _native_postprocess_filmGrain_setEnabled(enabled);
    }

    // Film grain intensity (0.0 - 1.0)
    public static function getFilmGrainIntensity(): float {
        return _native_postprocess_filmGrain_getIntensity();
    }

    public static function setFilmGrainIntensity(float value): void {
        _native_postprocess_filmGrain_setIntensity(value);
    }

    // Film grain particle size (0.5 - 5.0)
    public static function getFilmGrainSize(): float {
        return _native_postprocess_filmGrain_getSize();
    }

    public static function setFilmGrainSize(float value): void {
        _native_postprocess_filmGrain_setSize(value);
    }

    // ============================================
    // God Rays
    // ============================================

    public static function isGodRaysEnabled(): bool {
        return _native_postprocess_godRays_isEnabled();
    }

    public static function setGodRaysEnabled(bool enabled): void {
        _native_postprocess_godRays_setEnabled(enabled);
    }

    // God rays overall brightness (0.0 - 2.0)
    public static function getGodRaysIntensity(): float {
        return _native_postprocess_godRays_getIntensity();
    }

    public static function setGodRaysIntensity(float value): void {
        _native_postprocess_godRays_setIntensity(value);
    }

    // Exponential falloff per sample step (0.9 - 1.0)
    public static function getGodRaysDecay(): float {
        return _native_postprocess_godRays_getDecay();
    }

    public static function setGodRaysDecay(float value): void {
        _native_postprocess_godRays_setDecay(value);
    }

    // Sample step spacing (0.1 - 2.0)
    public static function getGodRaysDensity(): float {
        return _native_postprocess_godRays_getDensity();
    }

    public static function setGodRaysDensity(float value): void {
        _native_postprocess_godRays_setDensity(value);
    }

    // Per-sample contribution weight (0.0 - 2.0)
    public static function getGodRaysWeight(): float {
        return _native_postprocess_godRays_getWeight();
    }

    public static function setGodRaysWeight(float value): void {
        _native_postprocess_godRays_setWeight(value);
    }

    // Number of ray marching steps (16 - 128)
    public static function getGodRaysSampleCount(): int {
        return _native_postprocess_godRays_getSampleCount();
    }

    public static function setGodRaysSampleCount(int value): void {
        _native_postprocess_godRays_setSampleCount(value);
    }

    // Depth threshold for sky detection (0.0 - 1.0)
    public static function getGodRaysThreshold(): float {
        return _native_postprocess_godRays_getThreshold();
    }

    public static function setGodRaysThreshold(float value): void {
        _native_postprocess_godRays_setThreshold(value);
    }

    // ============================================
    // Depth of Field
    // ============================================

    public static function isDoFEnabled(): bool {
        return _native_postprocess_dof_isEnabled();
    }

    public static function setDoFEnabled(bool enabled): void {
        _native_postprocess_dof_setEnabled(enabled);
    }

    // Distance at which objects are in perfect focus (0.1 - 1000.0)
    public static function getDoFFocalDistance(): float {
        return _native_postprocess_dof_getFocalDistance();
    }

    public static function setDoFFocalDistance(float value): void {
        _native_postprocess_dof_setFocalDistance(value);
    }

    // Range around focal distance that stays sharp (0.1 - 100.0)
    public static function getDoFFocalRange(): float {
        return _native_postprocess_dof_getFocalRange();
    }

    public static function setDoFFocalRange(float value): void {
        _native_postprocess_dof_setFocalRange(value);
    }

    // Maximum blur radius in pixels (0.0 - 20.0)
    public static function getDoFMaxBlurRadius(): float {
        return _native_postprocess_dof_getMaxBlurRadius();
    }

    public static function setDoFMaxBlurRadius(float value): void {
        _native_postprocess_dof_setMaxBlurRadius(value);
    }

    // Number of Poisson disc samples (4 - 32)
    public static function getDoFSampleCount(): int {
        return _native_postprocess_dof_getSampleCount();
    }

    public static function setDoFSampleCount(int value): void {
        _native_postprocess_dof_setSampleCount(value);
    }
}
