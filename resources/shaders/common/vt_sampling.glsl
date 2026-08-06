#ifndef VT_SAMPLING_GLSL
#define VT_SAMPLING_GLSL

#include "vt_types.glsl"

// ============================================================
// Virtual Texturing (VK-1209) — virtual->physical translation, GLSL mirror of
// render::vt::VTAddressing (VTAddressing.hpp). Walks from the desired (finest) mip
// toward coarser mips until a resident page is found, then maps the logical page
// UV into that tile's bordered interior. With the coarsest mip pinned resident, a
// lookup always resolves — a streaming page reads briefly blurry, never as a hole.
//
// Requires the consumer to declare, before #include:
//   #define VT_PAGE_TABLE <name>   // a `uint <name>[];` SSBO member
// Optional, to emit page requests:
//   #define VT_FEEDBACK   <name>   // a `uint <name>[];` SSBO member (atomicOr target)
//
// VK-1620: the walk itself now lives in vt_lookup_impl.glsl, which is UNGUARDED and
// parameterized by VT_LOOKUP_FN / VT_WRITE_FEEDBACK_FN. This file is still the entry
// point and still emits the familiar `vtLookup` / `vtWriteFeedback` pair, so every
// existing consumer is untouched — but a shader that needs a SECOND virtual texture
// can now include the impl again under a different name. That is required because
// this file's include guard (and the fixed function names) made a second instance
// impossible, and the scene mesh shader must host material SVT and the terrain RVT
// at once. The stateless helpers below are instance-independent and stay here.
// ============================================================

struct VTSample
{
    vec2 uv;           // physical atlas UV [0,1)
    uint residentMip;  // mip actually found (>= desired)
    bool valid;
};

#ifdef VT_PAGE_TABLE
#ifndef VT_LOOKUP_FN
#define VT_LOOKUP_FN vtLookup
#endif
#ifdef VT_FEEDBACK
#ifndef VT_WRITE_FEEDBACK_FN
#define VT_WRITE_FEEDBACK_FN vtWriteFeedback
#endif
#endif
#include "vt_lookup_impl.glsl"
#endif // VT_PAGE_TABLE

// Desired mip from screen-space UV derivatives (fragment stage). virtualResTexels
// is the mip-0 texel resolution along the sampled axis. Mirrors
// VTAddressing::desiredMipFromDerivatives.
float vtDesiredMip(vec2 uv, float virtualResTexels)
{
    vec2 dx = dFdx(uv) * virtualResTexels;
    vec2 dy = dFdy(uv) * virtualResTexels;
    float m = max(length(dx), length(dy));
    return (m <= 1.0) ? 0.0 : log2(m);
}

// Feedback need only be emitted by a fraction of fragments: a visible page covers many pixels, so
// a few requests suffice, and full-rate atomicOr from every fragment causes heavy contention (all
// fragments of one page hit the same entry). ~1/64 of fragments (every 8th in x and y).
bool vtFeedbackFragment(vec2 fragCoord)
{
    return (uint(fragCoord.x) & 7u) == 0u && (uint(fragCoord.y) & 7u) == 0u;
}

#endif // VT_SAMPLING_GLSL
