#ifndef WATER_SHOALING_GLSL
#define WATER_SHOALING_GLSL

// VK-1605: shoaling + breaking shore waves.
//
// GPU twin of VFEngine/utilities/water/ShoalingMath.hpp and ShoreWaveMath.hpp. There is no codegen
// between them - the vertex stage displaces the surface with these functions and
// GPUDrivenRenderer::getOceanHeightAt feeds buoyancy with the C++ ones, so any edit here needs the
// matching edit there or floating bodies stop matching the water they float on.
//
// Requires water_params.glsl (for `ext`) to be included first.

layout(set = 9, binding = 3) uniform sampler2D shoreDepthTex;   // R32F, clamp-to-edge

#define SHORE_FIELD_DEEP   1.0e4
// MUST match water::SHORE_FIELD_RESOLUTION in VFEngine/utilities/water/ShoreDepthField.hpp — the
// texture is allocated at that size and only used here to derive a one-texel finite-difference step.
#define SHORE_FIELD_RESOLUTION 256.0
#define SHOALING_PI        3.14159265358979323846
#define SHOALING_TWO_PI    6.28318530717958647692
#define SHOALING_MAX_GAIN  3.0

// ext.shoreFieldOrigin = (originX, originZ, windowSize, 1/windowSize)
// ext.bandWavelength   = (lambda0, lambda1, lambda2, shoalingMinDepth)
// ext.shoreWaveA       = (amplitude, length, speed, breakDepth)
// ext.shoreWaveB       = (breakRange, crestFoam, crestFoamThreshold, shoreLean)

vec2 shoreFieldUV(vec2 worldXZ)
{
    return (worldXZ - ext.shoreFieldOrigin.xy) * ext.shoreFieldOrigin.w;
}

// Clamp-to-edge outside the window, exactly like ShoreDepthField::sample. The window fade below is
// what actually neutralises the extrapolated border values, on both the GPU and the CPU.
float shoreDepthAt(vec2 worldXZ)
{
    return texture(shoreDepthTex, shoreFieldUV(worldXZ)).r;
}

// Forward difference, reusing an already-taken centre sample so this costs only TWO extra taps.
// Points OFFSHORE (depth increases seaward), so the shoreward direction is its negation.
vec2 shoreDepthGradientFwd(vec2 worldXZ, float centerDepth, float eps)
{
    float e = max(eps, 1.0e-3);
    float dx = shoreDepthAt(worldXZ + vec2(e, 0.0)) - centerDepth;
    float dz = shoreDepthAt(worldXZ + vec2(0.0, e)) - centerDepth;
    return vec2(dx, dz) / e;
}

// 1 well inside the window, 0 at the border. Every shoreline effect is multiplied by this so that
// re-centring the window can never pop the surface (or a floating body) at the seam.
float shoreWindowFade(vec2 worldXZ)
{
    vec2 t = abs(shoreFieldUV(worldXZ) * 2.0 - 1.0);
    float edge = max(t.x, t.y);

    float e0 = clamp(ext.shoreEdgeFadeStart, 0.0, 0.999);
    if (edge <= e0) return 1.0;
    if (edge >= 1.0) return 0.0;

    float k = (edge - e0) / (1.0 - e0);
    return 1.0 - k * k * (3.0 - 2.0 * k);
}

// ---------------------------------------------------------------------------------------------
// Shoaling
// ---------------------------------------------------------------------------------------------

// Linear-theory shoaling coefficient Ks = sqrt(cg0/cg) as a function of k0*d = 2*pi*d/L0, using the
// Fenton & McKee (1990) explicit approximation for the finite-depth wavenumber.
float shoalingRawGain(float k0d)
{
    if (k0d < 1.0e-4) return SHOALING_MAX_GAIN;

    float kd = k0d / pow(tanh(pow(k0d, 0.75)), 2.0 / 3.0);
    float sinh2kd = sinh(2.0 * kd);
    float n = 0.5 * (1.0 + (sinh2kd > 1.0e-6 ? 2.0 * kd / sinh2kd : 1.0));
    return 1.0 / sqrt(max(2.0 * n * tanh(kd), 1.0e-6));
}

// EXACTLY 1.0 once depth >= wavelength/2 - which is what makes the deep-water side of the shore
// window byte-identical to the pre-VK-1605 surface. shoalingRawGain(PI) is the value at exactly
// depth = L/2 and folds to a constant; do not replace it with a transcribed literal, or this
// definition drifts from the C++ one.
float greensLawGain(float depth, float wavelength)
{
    if (wavelength <= 0.0) return 1.0;
    if (depth >= 0.5 * wavelength) return 1.0;
    if (depth <= 0.0) return SHOALING_MAX_GAIN;

    float k0d = SHOALING_TWO_PI * depth / wavelength;
    return clamp(shoalingRawGain(k0d) / shoalingRawGain(SHOALING_PI), 0.0, SHOALING_MAX_GAIN);
}

// McCowan's depth-limited breaking height, as a crest half-height.
float breakingAmplitudeLimit(float depth, float gamma, float minDepth)
{
    return 0.5 * max(gamma, 0.0) * max(depth - max(minDepth, 0.0), 0.0);
}

// Scale for one band's vertical displacement. bandAmplitude is that band's own disp.y, so the
// breaking cap applies to the real local wave. Returns exactly 1.0 in deep water for any strength.
float shoalingScale(float depth, float wavelength, float bandAmplitude, float strength)
{
    if (strength <= 0.0 || wavelength <= 0.0) return 1.0;

    float gain = greensLawGain(depth, wavelength);
    float a = abs(bandAmplitude);
    float limit = breakingAmplitudeLimit(depth, ext.shoalingGamma, ext.bandWavelength.w);

    float s = gain;
    if (a * gain > limit) s = (a > 1.0e-5) ? (limit / a) : 0.0;

    return 1.0 + (s - 1.0) * clamp(strength, 0.0, 1.0);
}

// Horizontal chop compression: the FFT chop is a shear that folds the mesh through the beach in
// shallow water. Zero derivative at both ends; the deep end is where the window border sits.
float chopCompression(float depth, float wavelength, float strength)
{
    if (strength <= 0.0 || wavelength <= 0.0) return 1.0;

    float dn = clamp(depth / (0.5 * wavelength), 0.0, 1.0);
    float c = dn * dn * (3.0 - 2.0 * dn);
    return 1.0 + (c - 1.0) * clamp(strength, 0.0, 1.0);
}

// ---------------------------------------------------------------------------------------------
// Breaking shore waves
//
// The phase coordinate is water DEPTH, so crests are iso-depth contours: breakers arrive parallel
// to the shoreline and wrap around headlands with no bathymetry solve.
// ---------------------------------------------------------------------------------------------

float shoreWaveEnvelope(float depth)
{
    float amplitude = ext.shoreWaveA.x;
    float breakRange = ext.shoreWaveB.x;
    if (amplitude <= 0.0 || breakRange <= 0.0 || depth <= 0.0) return 0.0;

    float breakDepth = max(ext.shoreWaveA.w, breakRange);
    float rise = smoothstep(0.0, breakRange, depth);
    float fall = 1.0 - smoothstep(breakDepth, breakDepth + breakRange, depth);
    return rise * fall;
}

// '+' so crests travel SHOREWARD: holding phase constant gives depth = L*(c - t*speed).
float shoreWavePhase(float depth, float time)
{
    float waveLength = max(ext.shoreWaveA.y, 1.0e-3);
    return SHOALING_TWO_PI * (depth / waveLength + time * ext.shoreWaveA.z);
}

// [0,1], flat troughs and a sharp crest - a bore that only ever adds water, never a symmetric sine.
float shoreWaveProfile(float phase)
{
    float s = 0.5 * (sin(phase) + 1.0);
    return s * s * s;
}

float shoreWaveHeight(float depth, float time)
{
    float env = shoreWaveEnvelope(depth);
    if (env <= 0.0) return 0.0;
    return ext.shoreWaveA.x * env * shoreWaveProfile(shoreWavePhase(depth, time));
}

float shoreCrestFoam(float depth, float time)
{
    if (ext.shoreWaveB.y <= 0.0) return 0.0;

    float env = shoreWaveEnvelope(depth);
    if (env <= 0.0) return 0.0;

    float profile = shoreWaveProfile(shoreWavePhase(depth, time));
    return smoothstep(clamp(ext.shoreWaveB.z, 0.0, 0.99), 1.0, profile) * env * ext.shoreWaveB.y;
}

#endif // WATER_SHOALING_GLSL
