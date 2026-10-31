// motion_vectors.glsl
// THE engine motion-vector convention (VK-1662). CPU twin: render::upscaling::motionVectorNdc in
// VFEngine/graphics/render/upscaling/TemporalUpscaleMath.hpp - keep the two identical (test_upscale_motion_vectors).
//
// DLSS (through Streamline) wants each vector to map the pixel's CURRENT position to its PREVIOUS one
// (previous - current, DLSS SR Programming Guide section 3.6), with the sub-pixel jitter removed from both ends
// (motionVectorsJittered = false). Vectors are stored in NDC with +y down (Vulkan, positive-height viewports);
// UpscaleManager's mvecScale = 0.5 converts NDC to UV and Streamline multiplies by the render size.

// Per component: a current NDC lies in [-1, 1], so a component of magnitude 4 puts the previous position at least
// 3 NDC units away on that axis, i.e. off-screen. Clamping there keeps the on/off-screen classification exact and
// the rg16f target finite as the previous w nears 0; (MV_MAX_NDC, MV_MAX_NDC) is also the "no history" marker.
const float MV_MAX_NDC = 4.0;

vec2 motionVectorNdc(vec4 currentClip, vec4 previousClip)
{
    // On or behind the previous camera's eye plane the point had no previous position on screen: point off-screen
    // so DLSS rejects the history, exactly as for a point just in front of that plane (which clamps).
    if (previousClip.w <= 0.0)
        return vec2(MV_MAX_NDC);

    vec2 motion = previousClip.xy / previousClip.w - currentClip.xy / currentClip.w;
    return clamp(motion, vec2(-MV_MAX_NDC), vec2(MV_MAX_NDC));
}
