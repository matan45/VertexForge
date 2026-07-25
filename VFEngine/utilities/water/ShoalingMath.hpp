#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace water
{
    // VK-1605: CPU twin of resources/shaders/water/water_shoaling.glsl. Keep both in sync - the
    // vertex shader displaces the water surface with these functions and getOceanHeightAt feeds
    // buoyancy with the same ones, so any drift between them puts floating bodies at a different
    // height than the water they are floating on.
    //
    // Model: a wave "feels the bottom" once the depth drops below half its wavelength. From there
    // linear theory says the amplitude first dips slightly and then GROWS (Green's law,
    // A ~ d^-1/4). It cannot grow forever: a wave breaks once its height reaches roughly 0.78 of
    // the water depth (McCowan's criterion), after which the surf zone caps it and it collapses to
    // nothing at the waterline. Modelling both halves is what produces a shoreline that rears up
    // and then flattens, instead of waves that simply fade out - and the breaking cap is also what
    // stops the displaced mesh punching through the terrain.

    inline constexpr float SHOALING_PI = 3.14159265358979323846f;
    inline constexpr float SHOALING_TWO_PI = 6.28318530717958647692f;

    // Green's-law gain is unbounded as depth -> 0; the breaking cap normally takes over long before
    // that, but the raw gain still has to be finite for the min() to be meaningful.
    inline constexpr float SHOALING_MAX_GAIN = 3.0f;

    // McCowan's depth-limited breaking ratio H/d.
    inline constexpr float SHOALING_DEFAULT_GAMMA = 0.78f;

    // Pierson-Moskowitz peak wavelength for a fully developed sea at wind speed U (m/s).
    // The FFT bands are already built from a PM-family spectrum, so this is the wavelength that
    // actually carries the band's energy - unlike patchSize, which is only a tiling period.
    //   omega_p = 0.877 g / U  and deep water lambda = 2 pi g / omega^2
    //   => lambda_p = 2 pi U^2 / (0.877^2 g)
    // 12 / 8 / 4 m/s (the default band winds) give 120 / 53 / 13 m, so swell starts shoaling in
    // 60 m of water while ripples wait until 6.7 m - the ordering a beach actually shows.
    inline float characteristicWavelength(float windSpeed, float gravity = 9.81f)
    {
        const float u = std::max(windSpeed, 0.0f);
        const float g = std::max(gravity, 0.01f);
        return SHOALING_TWO_PI * u * u / (0.877f * 0.877f * g);
    }

    // Raw linear-theory shoaling coefficient Ks = sqrt(cg0 / cg) as a function of the DEEP-water
    // relative depth k0*d = 2*pi*d/L0.
    //
    // The finite-depth wavenumber solves (kd)tanh(kd) = k0d, which has no closed form; this uses
    // the Fenton & McKee (1990) explicit approximation, accurate to well under 1% over the whole
    // range. Everything downstream is normalised by this same function, so the approximation error
    // cancels at the deep-water boundary instead of showing up as a seam.
    inline float shoalingRawGain(float k0d)
    {
        if (k0d < 1.0e-4f)
            return SHOALING_MAX_GAIN;

        const float kd = k0d / std::pow(std::tanh(std::pow(k0d, 0.75f)), 2.0f / 3.0f);
        const float sinh2kd = std::sinh(2.0f * kd);
        const float n = 0.5f * (1.0f + (sinh2kd > 1.0e-6f ? 2.0f * kd / sinh2kd : 1.0f));
        return 1.0f / std::sqrt(std::max(2.0f * n * std::tanh(kd), 1.0e-6f));
    }

    // Amplitude gain from shoaling. EXACTLY 1.0 once depth >= wavelength/2, which is what lets the
    // shore field fade out at its window border without a visible step: outside the window the
    // depth reads SHORE_FIELD_DEEP and this returns literal 1.0f, not 0.99-something.
    //
    // Note shoalingRawGain(PI) is the value at exactly depth = L/2; dividing by it (a constant the
    // compiler folds away) is what makes the two branches meet. Do NOT replace it with a hardcoded
    // literal - the GLSL twin evaluates the same expression, and a transcribed constant would let
    // the two definitions drift.
    inline float greensLawGain(float depth, float wavelength)
    {
        if (wavelength <= 0.0f)
            return 1.0f;
        if (depth >= 0.5f * wavelength)
            return 1.0f;
        if (depth <= 0.0f)
            return SHOALING_MAX_GAIN;

        const float k0d = SHOALING_TWO_PI * depth / wavelength;
        const float g = shoalingRawGain(k0d) / shoalingRawGain(SHOALING_PI);
        return std::clamp(g, 0.0f, SHOALING_MAX_GAIN);
    }

    // Largest crest half-height this depth can support before the wave breaks. minDepth pushes the
    // fully-flat line offshore (0 = the wave survives right up to the waterline).
    inline float breakingAmplitudeLimit(float depth, float gamma, float minDepth)
    {
        return 0.5f * std::max(gamma, 0.0f) * std::max(depth - std::max(minDepth, 0.0f), 0.0f);
    }

    // Scale for one band's VERTICAL displacement at this point. `bandAmplitude` is that band's own
    // displacement sample, which both the shader (disp.y) and the CPU sampler (sampleHeightAt) have
    // to hand, so the breaking cap can be applied to the real local wave rather than a guess.
    //
    // Returns exactly 1.0f in deep water for ANY strength: the gain is exactly 1 there and the
    // breaking limit (>= 0.195 * wavelength, i.e. ~23 m for swell) can never bind.
    inline float shoalingScale(float depth, float wavelength, float bandAmplitude,
                               float strength, float gamma, float minDepth)
    {
        if (strength <= 0.0f || wavelength <= 0.0f)
            return 1.0f;

        const float gain = greensLawGain(depth, wavelength);
        const float a = std::abs(bandAmplitude);
        const float limit = breakingAmplitudeLimit(depth, gamma, minDepth);

        float s = gain;
        if (a * gain > limit)
            s = (a > 1.0e-5f) ? (limit / a) : 0.0f;

        return 1.0f + (s - 1.0f) * std::clamp(strength, 0.0f, 1.0f);
    }

    // Horizontal chop compression. Real waves get more peaked before they break, but the FFT chop
    // is a horizontal shear: left alone in shallow water it folds the mesh through itself and
    // through the beach. Smoothstep so the derivative is zero at both ends - the deep-water end is
    // where the shore-field window border sits.
    inline float chopCompression(float depth, float wavelength, float strength)
    {
        if (strength <= 0.0f || wavelength <= 0.0f)
            return 1.0f;

        const float dn = std::clamp(depth / (0.5f * wavelength), 0.0f, 1.0f);
        const float c = dn * dn * (3.0f - 2.0f * dn);
        return 1.0f + (c - 1.0f) * std::clamp(strength, 0.0f, 1.0f);
    }

    // 1 well inside the shore-depth window, 0 at its border. Every shoreline effect is multiplied
    // by this, so when the window re-centres the newly covered area contributes nothing at first
    // and fades in - which is the whole mechanism behind "no buoyancy pop at the window edge".
    inline float shoreWindowFade(const glm::vec2& worldXZ, const glm::vec2& origin,
                                 float windowSize, float fadeStart)
    {
        if (windowSize <= 0.0f)
            return 0.0f;

        const glm::vec2 uv = (worldXZ - origin) / windowSize;
        const glm::vec2 t = glm::abs(uv * 2.0f - 1.0f);   // 0 at the centre, 1 at the border
        const float edge = std::max(t.x, t.y);

        const float e0 = std::clamp(fadeStart, 0.0f, 0.999f);
        if (edge <= e0)
            return 1.0f;
        if (edge >= 1.0f)
            return 0.0f;

        const float k = (edge - e0) / (1.0f - e0);
        return 1.0f - k * k * (3.0f - 2.0f * k);
    }
}
