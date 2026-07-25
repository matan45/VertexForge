#pragma once

#include "ShoalingMath.hpp"

#include <algorithm>
#include <cmath>

namespace water
{
    // VK-1605: CPU twin of the shore-wave half of resources/shaders/water/water_shoaling.glsl.
    //
    // The FFT bands cannot produce breakers: their spectrum is periodic in patch space and has no
    // idea where the shore is. This is a separate, purely analytic deformer that rides on top -
    // a surge of water travelling up the beach, confined to the surf zone by a depth envelope.
    //
    // The one idea worth keeping in mind: the phase coordinate is WATER DEPTH, not distance. That
    // makes every crest an iso-depth contour, so breakers automatically arrive parallel to the
    // shoreline and wrap around headlands - the visible result of wave refraction - for free, with
    // no bathymetry solve.

    struct ShoreWaveParams
    {
        float amplitude = 0.0f;          // metres of crest height; 0 disables everything
        float length = 12.0f;            // metres of DEPTH between successive crests
        float speed = 0.35f;             // crests per second (positive = travelling shoreward)
        float breakDepth = 1.5f;         // depth at which the surf zone ends (offshore edge)
        float breakRange = 1.0f;         // width of the fade in at the waterline and out to sea
        float crestFoam = 0.6f;          // foam added at the crest
        float crestFoamThreshold = 0.55f;
    };

    namespace detail
    {
        inline float shoreSmoothstep(float edge0, float edge1, float x)
        {
            if (edge1 <= edge0)
                return x < edge0 ? 0.0f : 1.0f;
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    // EXACTLY zero on land (depth <= 0) and in water deeper than breakDepth + breakRange. Both
    // ends are hard zeros rather than an exponential tail: the deep end has to vanish completely
    // or breakers would be riding the open ocean, and the land end has to vanish completely or the
    // deformer would lift water above the beach.
    inline float shoreWaveEnvelope(float depth, const ShoreWaveParams& p)
    {
        if (p.amplitude <= 0.0f || p.breakRange <= 0.0f || depth <= 0.0f)
            return 0.0f;

        // A breakDepth inside the fade-in width would make the two ramps overlap and clip the
        // plateau away entirely; push it out instead of producing a silently dead envelope.
        const float breakDepth = std::max(p.breakDepth, p.breakRange);

        const float rise = detail::shoreSmoothstep(0.0f, p.breakRange, depth);
        const float fall = 1.0f - detail::shoreSmoothstep(breakDepth, breakDepth + p.breakRange, depth);
        return rise * fall;
    }

    // Crests travel SHOREWARD: holding the phase constant gives depth = length*(c - time*speed),
    // which decreases as time advances. (The '+' matters - a '-' sends the surf back out to sea.)
    inline float shoreWavePhase(float depth, float time, const ShoreWaveParams& p)
    {
        const float length = std::max(p.length, 1.0e-3f);
        return SHOALING_TWO_PI * (depth / length + time * p.speed);
    }

    // In [0,1] with flat troughs and a sharp crest. A broken wave is a bore - a surge that only
    // ever adds water - not a symmetric sine, so this never goes negative and the whole deformer
    // is a strictly additive height.
    inline float shoreWaveProfile(float phase)
    {
        const float s = 0.5f * (std::sin(phase) + 1.0f);
        return s * s * s;
    }

    inline float shoreWaveHeight(float depth, float time, const ShoreWaveParams& p)
    {
        const float env = shoreWaveEnvelope(depth, p);
        if (env <= 0.0f)
            return 0.0f;
        return p.amplitude * env * shoreWaveProfile(shoreWavePhase(depth, time, p));
    }

    inline float shoreCrestFoam(float depth, float time, const ShoreWaveParams& p)
    {
        if (p.crestFoam <= 0.0f)
            return 0.0f;

        const float env = shoreWaveEnvelope(depth, p);
        if (env <= 0.0f)
            return 0.0f;

        const float profile = shoreWaveProfile(shoreWavePhase(depth, time, p));
        return detail::shoreSmoothstep(std::clamp(p.crestFoamThreshold, 0.0f, 0.99f), 1.0f, profile)
             * env * p.crestFoam;
    }
}
