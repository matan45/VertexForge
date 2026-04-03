// PostProcess - Static utility class for post-processing settings
// Controls all post-process effects: tone mapping, TAA, bloom, vignette,
// chromatic aberration, film grain, color grading, and more.
//
// Usage examples:
//   PostProcess::setEnabled(true);
//   PostProcess::setToneMappingMode(ToneMappingMode::ACES);
//   PostProcess::setExposure(1.5);
//   PostProcess::setBloomEnabled(true);
//   PostProcess::setBloomIntensity(0.8);
//   PostProcess::setColorGradingEnabled(true);
//   PostProcess::setColorGradingSaturation(1.2);

import * from "../math/Vec3f.mt";

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

    // Toe: shadow curve strength (0.0 - 1.0, 0.0 = neutral)
    public static function getToe(): float {
        return _native_postprocess_toneMapping_getToe();
    }

    public static function setToe(float value): void {
        _native_postprocess_toneMapping_setToe(value);
    }

    // Shoulder: highlight curve strength (0.0 - 1.0, 0.0 = neutral)
    public static function getShoulder(): float {
        return _native_postprocess_toneMapping_getShoulder();
    }

    public static function setShoulder(float value): void {
        _native_postprocess_toneMapping_setShoulder(value);
    }

    // ============================================
    // TAA (Temporal Anti-Aliasing)
    // ============================================

    public static function isTAAEnabled(): bool {
        return _native_postprocess_taa_isEnabled();
    }

    public static function setTAAEnabled(bool enabled): void {
        _native_postprocess_taa_setEnabled(enabled);
    }

    // TAA blend factor: lower = more temporal smoothing (0.01 - 0.5)
    public static function getTAABlendFactor(): float {
        return _native_postprocess_taa_getBlendFactor();
    }

    public static function setTAABlendFactor(float value): void {
        _native_postprocess_taa_setBlendFactor(value);
    }

    // TAA sharpening strength (0.0 - 1.0)
    public static function getTAASharpenStrength(): float {
        return _native_postprocess_taa_getSharpenStrength();
    }

    public static function setTAASharpenStrength(float value): void {
        _native_postprocess_taa_setSharpenStrength(value);
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
    // Depth of Field
    // ============================================

    public static function isDoFEnabled(): bool {
        return _native_postprocess_dof_isEnabled();
    }

    public static function setDoFEnabled(bool enabled): void {
        _native_postprocess_dof_setEnabled(enabled);
    }

    // Focus mode: 0 = Manual, 1 = TargetPoint
    public static function getDoFFocusMode(): int {
        return _native_postprocess_dof_getFocusMode();
    }

    public static function setDoFFocusMode(int mode): void {
        _native_postprocess_dof_setFocusMode(mode);
    }

    // Distance at which objects are in perfect focus (0.1 - 1000.0)
    public static function getDoFFocalDistance(): float {
        return _native_postprocess_dof_getFocalDistance();
    }

    public static function setDoFFocalDistance(float value): void {
        _native_postprocess_dof_setFocalDistance(value);
    }

    // World-space focus target position (used in TargetPoint mode)
    public static function getDoFFocusTargetX(): float {
        return _native_postprocess_dof_getFocusTargetX();
    }

    public static function getDoFFocusTargetY(): float {
        return _native_postprocess_dof_getFocusTargetY();
    }

    public static function getDoFFocusTargetZ(): float {
        return _native_postprocess_dof_getFocusTargetZ();
    }

    public static function getDoFFocusTarget(): Vec3f {
        return new Vec3f(
            _native_postprocess_dof_getFocusTargetX(),
            _native_postprocess_dof_getFocusTargetY(),
            _native_postprocess_dof_getFocusTargetZ()
        );
    }

    public static function setDoFFocusTarget(float x, float y, float z): void {
        _native_postprocess_dof_setFocusTarget(x, y, z);
    }

    public static function setDoFFocusTargetVec(Vec3f target): void {
        _native_postprocess_dof_setFocusTarget(target.x, target.y, target.z);
    }

    // How quickly focus transitions to target (0.1 - 50.0)
    public static function getDoFFocusSmoothing(): float {
        return _native_postprocess_dof_getFocusSmoothing();
    }

    public static function setDoFFocusSmoothing(float value): void {
        _native_postprocess_dof_setFocusSmoothing(value);
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

    // ============================================
    // Volumetric Fog
    // ============================================

    public static function isVolumetricFogEnabled(): bool {
        return _native_postprocess_volumetricFog_isEnabled();
    }

    public static function setVolumetricFogEnabled(bool enabled): void {
        _native_postprocess_volumetricFog_setEnabled(enabled);
    }

    // Quality: 0 = Low, 1 = Medium, 2 = High
    public static function getVolumetricFogQuality(): int {
        return _native_postprocess_volumetricFog_getQuality();
    }

    public static function setVolumetricFogQuality(int quality): void {
        _native_postprocess_volumetricFog_setQuality(quality);
    }

    // Uniform fog density (0.0 - 1.0)
    public static function getVolumetricFogDensity(): float {
        return _native_postprocess_volumetricFog_getDensity();
    }

    public static function setVolumetricFogDensity(float value): void {
        _native_postprocess_volumetricFog_setDensity(value);
    }

    // Fog color (RGB, 0.0 - 1.0 each)
    public static function getVolumetricFogColorR(): float {
        return _native_postprocess_volumetricFog_getColorR();
    }

    public static function getVolumetricFogColorG(): float {
        return _native_postprocess_volumetricFog_getColorG();
    }

    public static function getVolumetricFogColorB(): float {
        return _native_postprocess_volumetricFog_getColorB();
    }

    public static function getVolumetricFogColor(): Vec3f {
        return new Vec3f(
            _native_postprocess_volumetricFog_getColorR(),
            _native_postprocess_volumetricFog_getColorG(),
            _native_postprocess_volumetricFog_getColorB()
        );
    }

    public static function setVolumetricFogColor(float r, float g, float b): void {
        _native_postprocess_volumetricFog_setColor(r, g, b);
    }

    public static function setVolumetricFogColorVec(Vec3f color): void {
        _native_postprocess_volumetricFog_setColor(color.x, color.y, color.z);
    }

    // Height fog density (0.0 - 1.0)
    public static function getVolumetricFogHeightDensity(): float {
        return _native_postprocess_volumetricFog_getHeightFogDensity();
    }

    public static function setVolumetricFogHeightDensity(float value): void {
        _native_postprocess_volumetricFog_setHeightFogDensity(value);
    }

    // Height fog falloff rate (0.0 - 1.0)
    public static function getVolumetricFogHeightFalloff(): float {
        return _native_postprocess_volumetricFog_getHeightFogFalloff();
    }

    public static function setVolumetricFogHeightFalloff(float value): void {
        _native_postprocess_volumetricFog_setHeightFogFalloff(value);
    }

    // Height fog offset (world Y position, -100.0 - 500.0)
    public static function getVolumetricFogHeightOffset(): float {
        return _native_postprocess_volumetricFog_getHeightFogOffset();
    }

    public static function setVolumetricFogHeightOffset(float value): void {
        _native_postprocess_volumetricFog_setHeightFogOffset(value);
    }

    // Scattering coefficient (0.0 - 5.0)
    public static function getVolumetricFogScattering(): float {
        return _native_postprocess_volumetricFog_getScatteringCoefficient();
    }

    public static function setVolumetricFogScattering(float value): void {
        _native_postprocess_volumetricFog_setScatteringCoefficient(value);
    }

    // Absorption coefficient (0.0 - 5.0)
    public static function getVolumetricFogAbsorption(): float {
        return _native_postprocess_volumetricFog_getAbsorptionCoefficient();
    }

    public static function setVolumetricFogAbsorption(float value): void {
        _native_postprocess_volumetricFog_setAbsorptionCoefficient(value);
    }

    // Anisotropy: directional scattering bias (-1.0 to 1.0)
    public static function getVolumetricFogAnisotropy(): float {
        return _native_postprocess_volumetricFog_getAnisotropy();
    }

    public static function setVolumetricFogAnisotropy(float value): void {
        _native_postprocess_volumetricFog_setAnisotropy(value);
    }

    // Overall fog intensity multiplier (0.0 - 5.0)
    public static function getVolumetricFogIntensity(): float {
        return _native_postprocess_volumetricFog_getIntensity();
    }

    public static function setVolumetricFogIntensity(float value): void {
        _native_postprocess_volumetricFog_setIntensity(value);
    }

    // Ambient light contribution in fog (0.0 - 1.0)
    public static function getVolumetricFogAmbientIntensity(): float {
        return _native_postprocess_volumetricFog_getAmbientIntensity();
    }

    public static function setVolumetricFogAmbientIntensity(float value): void {
        _native_postprocess_volumetricFog_setAmbientIntensity(value);
    }

    // Maximum fog render distance (10.0 - 5000.0)
    public static function getVolumetricFogMaxDistance(): float {
        return _native_postprocess_volumetricFog_getMaxDistance();
    }

    public static function setVolumetricFogMaxDistance(float value): void {
        _native_postprocess_volumetricFog_setMaxDistance(value);
    }

    // Temporal blend factor for smoothing (0.0 - 1.0)
    public static function getVolumetricFogTemporalBlend(): float {
        return _native_postprocess_volumetricFog_getTemporalBlendFactor();
    }

    public static function setVolumetricFogTemporalBlend(float value): void {
        _native_postprocess_volumetricFog_setTemporalBlendFactor(value);
    }

    // ============================================
    // SSAO (Screen-Space Ambient Occlusion)
    // ============================================

    public static function isSSAOEnabled(): bool {
        return _native_postprocess_ssao_isEnabled();
    }

    public static function setSSAOEnabled(bool enabled): void {
        _native_postprocess_ssao_setEnabled(enabled);
    }

    // World-space sample radius (0.1 - 5.0)
    public static function getSSAORadius(): float {
        return _native_postprocess_ssao_getRadius();
    }

    public static function setSSAORadius(float value): void {
        _native_postprocess_ssao_setRadius(value);
    }

    // Depth comparison bias (0.001 - 0.1)
    public static function getSSAOBias(): float {
        return _native_postprocess_ssao_getBias();
    }

    public static function setSSAOBias(float value): void {
        _native_postprocess_ssao_setBias(value);
    }

    // AO darkening strength (0.1 - 5.0)
    public static function getSSAOIntensity(): float {
        return _native_postprocess_ssao_getIntensity();
    }

    public static function setSSAOIntensity(float value): void {
        _native_postprocess_ssao_setIntensity(value);
    }

    // Hemisphere samples (8 - 64)
    public static function getSSAOKernelSize(): int {
        return _native_postprocess_ssao_getKernelSize();
    }

    public static function setSSAOKernelSize(int value): void {
        _native_postprocess_ssao_setKernelSize(value);
    }

    // Quality preset (0=Low, 1=Medium, 2=High, 3=Ultra)
    public static function getSSAOQuality(): int {
        return _native_postprocess_ssao_getQuality();
    }

    public static function setSSAOQuality(int value): void {
        _native_postprocess_ssao_setQuality(value);
    }

    // Contrast power curve (0.5 - 5.0)
    public static function getSSAOPower(): float {
        return _native_postprocess_ssao_getPower();
    }

    public static function setSSAOPower(float value): void {
        _native_postprocess_ssao_setPower(value);
    }

    // ============================================
    // Edge Detection
    // ============================================

    public static function isEdgeDetectionEnabled(): bool {
        return _native_postprocess_edgeDetection_isEnabled();
    }

    public static function setEdgeDetectionEnabled(bool enabled): void {
        _native_postprocess_edgeDetection_setEnabled(enabled);
    }

    // Edge sensitivity threshold (0.01 - 1.0)
    public static function getEdgeDetectionThreshold(): float {
        return _native_postprocess_edgeDetection_getThreshold();
    }

    public static function setEdgeDetectionThreshold(float value): void {
        _native_postprocess_edgeDetection_setThreshold(value);
    }

    // Edge line thickness (0.5 - 3.0)
    public static function getEdgeDetectionWidth(): float {
        return _native_postprocess_edgeDetection_getEdgeWidth();
    }

    public static function setEdgeDetectionWidth(float value): void {
        _native_postprocess_edgeDetection_setEdgeWidth(value);
    }

    // Edge color (RGB, 0.0 - 1.0 each)
    public static function getEdgeDetectionColorR(): float {
        return _native_postprocess_edgeDetection_getColorR();
    }

    public static function getEdgeDetectionColorG(): float {
        return _native_postprocess_edgeDetection_getColorG();
    }

    public static function getEdgeDetectionColorB(): float {
        return _native_postprocess_edgeDetection_getColorB();
    }

    public static function getEdgeDetectionColor(): Vec3f {
        return new Vec3f(
            _native_postprocess_edgeDetection_getColorR(),
            _native_postprocess_edgeDetection_getColorG(),
            _native_postprocess_edgeDetection_getColorB()
        );
    }

    public static function setEdgeDetectionColor(float r, float g, float b): void {
        _native_postprocess_edgeDetection_setColor(r, g, b);
    }

    public static function setEdgeDetectionColorVec(Vec3f color): void {
        _native_postprocess_edgeDetection_setColor(color.x, color.y, color.z);
    }

    // Edge overlay opacity (0.0 - 1.0)
    public static function getEdgeDetectionOpacity(): float {
        return _native_postprocess_edgeDetection_getOpacity();
    }

    public static function setEdgeDetectionOpacity(float value): void {
        _native_postprocess_edgeDetection_setOpacity(value);
    }

    // ============================================
    // Color Grading
    // ============================================

    public static function isColorGradingEnabled(): bool {
        return _native_postprocess_colorGrading_isEnabled();
    }

    public static function setColorGradingEnabled(bool enabled): void {
        _native_postprocess_colorGrading_setEnabled(enabled);
    }

    // Saturation: 0 = grayscale, 1 = neutral, 3 = max (0.0 - 3.0)
    public static function getColorGradingSaturation(): float {
        return _native_postprocess_colorGrading_getSaturation();
    }

    public static function setColorGradingSaturation(float value): void {
        _native_postprocess_colorGrading_setSaturation(value);
    }

    // Color temperature in Kelvin. 6500 = neutral daylight (1000 - 15000)
    public static function getColorGradingTemperature(): float {
        return _native_postprocess_colorGrading_getColorTemperature();
    }

    public static function setColorGradingTemperature(float value): void {
        _native_postprocess_colorGrading_setColorTemperature(value);
    }

    // Green-magenta tint (-1.0 to 1.0, 0 = neutral)
    public static function getColorGradingTint(): float {
        return _native_postprocess_colorGrading_getColorTint();
    }

    public static function setColorGradingTint(float value): void {
        _native_postprocess_colorGrading_setColorTint(value);
    }

    // LUT intensity: 0 = no LUT effect, 1 = full LUT (0.0 - 1.0)
    public static function getColorGradingLutIntensity(): float {
        return _native_postprocess_colorGrading_getLutIntensity();
    }

    public static function setColorGradingLutIntensity(float value): void {
        _native_postprocess_colorGrading_setLutIntensity(value);
    }

    // LUT blend factor: 0 = primary only, 1 = secondary only (0.0 - 1.0)
    public static function getColorGradingLutBlendFactor(): float {
        return _native_postprocess_colorGrading_getLutBlendFactor();
    }

    public static function setColorGradingLutBlendFactor(float value): void {
        _native_postprocess_colorGrading_setLutBlendFactor(value);
    }
}
