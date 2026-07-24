#pragma once

// VK-1585 — pure, header-only biome compositing math for the scatter bakers.
//
// The invariant: biome edge-blend must be a CONTINUOUS function of the terrain splat weight, never
// a random reroll — so the deterministic per-cell KEEP value stays fixed and only the keep-PROBABILITY
// (the threshold) moves. As a biome's layer weight ramps 1->0 across a transition, keepProb falls
// smoothly and progressively fewer of the same cells survive: a natural density falloff, seam-free
// (splat weightmaps are bilinearly continuous across tile boundaries) and order-independent.

#include <algorithm>

namespace vegetation
{
    // Continuous biome membership in [0,1] from a splat weight in [0,1]. smoothstep around 0.5 gives
    // a C1-continuous, monotonic ramp; edgeBlendWidth widens the transition band (0 ⇒ hard step at 0.5).
    inline float biomeMembership(float layerWeight, float edgeBlendWidth)
    {
        const float half = 0.5f * std::clamp(edgeBlendWidth, 0.0f, 1.0f);
        const float lo = 0.5f - half;
        const float hi = 0.5f + half;
        if (hi <= lo)
            return layerWeight >= 0.5f ? 1.0f : 0.0f; // width 0 -> hard step
        const float t = std::clamp((layerWeight - lo) / (hi - lo), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t); // smoothstep
    }

    // Effective per-cell keep-probability for a rule in a biome, given the biome's membership and the
    // max membership of any HIGHER-priority biome at this position (soft priority suppression). In a
    // biome interior (membership~1, higherPriorityMembership~0) this is the full rule density; a
    // higher-priority biome carves out lower ones with a soft edge as its membership ->1.
    inline float biomeEffectiveKeepProb(float ruleDensity, float biomeDensityScale, float globalDensityScale,
                                        float membership, float higherPriorityMembership)
    {
        const float p = ruleDensity * biomeDensityScale * globalDensityScale
                        * membership * (1.0f - higherPriorityMembership);
        return std::clamp(p, 0.0f, 1.0f);
    }
}
