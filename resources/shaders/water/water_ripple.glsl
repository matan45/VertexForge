#ifndef WATER_RIPPLE_GLSL
#define WATER_RIPPLE_GLSL

// VK-1606: the interactive ripple patch - a camera-following window of damped-wave-equation water
// that boats, splashes and scripted impulses write into.
//
// The simulation itself lives in ripple_sim.glsl (compute) and its CPU twin
// VFEngine/utilities/water/RippleSimMath.hpp. This header is only the read side: the water vertex
// stage displaces geometry with the height, and the fragment stage adds the foam.
//
// Deliberately NOT part of CPU buoyancy: GPUDrivenRenderer::getOceanHeightAt sums the FFT bands and
// the shoaling terms but never this, so a boat does not float on its own wake. Feeding it back would
// need a per-frame readback of the whole patch and would close a physical feedback loop.
//
// Requires water_params.glsl (for `ext`) to be included first.

// R = height (simulation units), GB = surface-gradient normal offset, A = foam.
layout(set = 9, binding = 4) uniform sampler2D rippleTex;

// ext.ripplePatch  = (originX, originZ, patchSize, 1/patchSize)
// ext.rippleParams = (heightScale, normalScale, foamScale, edgeFadeStart)

vec2 rippleUV(vec2 worldXZ)
{
    return (worldXZ - ext.ripplePatch.xy) * ext.ripplePatch.w;
}

// 1 well inside the patch, 0 at its border. Same smoothstep shape as shoreWindowFade in
// water_shoaling.glsl and for the same reason: the patch only covers ~100 m of a surface that
// stretches to the horizon, so without this the water would show a hard square seam around the
// camera. Multiplying the CONTRIBUTION (rather than clamping the sample) is what makes the border
// exactly neutral.
float rippleWindowFade(vec2 worldXZ)
{
    vec2 t = abs(rippleUV(worldXZ) * 2.0 - 1.0);
    float edge = max(t.x, t.y);

    float e0 = clamp(ext.rippleParams.w, 0.0, 0.999);
    if (edge <= e0) return 1.0;
    if (edge >= 1.0) return 0.0;

    float k = (edge - e0) / (1.0 - e0);
    return 1.0 - k * k * (3.0 - 2.0 * k);
}

// Clamp-to-edge outside the patch; the fade above is what neutralises the extrapolated border.
vec4 rippleSampleRaw(vec2 worldXZ)
{
    return texture(rippleTex, rippleUV(worldXZ));
}

// Vertical displacement to add to the FFT sum, already faded and scaled.
float rippleHeightAt(vec2 worldXZ)
{
    return rippleSampleRaw(worldXZ).x * ext.rippleParams.x * rippleWindowFade(worldXZ);
}

#endif // WATER_RIPPLE_GLSL
