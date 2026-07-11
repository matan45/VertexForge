#type FRAGMENT
#version 460 core

// VK-1490 editor selection mask, visible-only test. The scene depth is SAMPLED
// (not bound as a depth attachment) and the fragment passes when it is within
// a bias of the FARTHEST depth in the 3x3 neighborhood. A strict per-pixel
// depth test cannot work here: the mask re-rasterizes the geometry through a
// different pipeline (not depth-invariant) and under MSAA the resolved depth
// belongs to sample 0, so subpixel/high-frequency submeshes (ore piles, small
// rocks) speckle and flicker with camera movement. The neighborhood tolerance
// accepts anything within ~1px of a visible surface while genuinely occluded
// surfaces still discard; the cost is the outline bleeding ~1px across
// occluder silhouettes, invisible in practice.
layout(location = 0) out vec4 outMask;

layout(set = 6, binding = 1) uniform sampler2D sceneDepth;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

void main() {
    ivec2 depthExtent = textureSize(sceneDepth, 0);
    vec2 uv = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
    ivec2 center = ivec2(floor(uv * vec2(depthExtent)));
    center = clamp(center, ivec2(0), depthExtent - ivec2(1));

    float sceneZ = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 sampleCoord = clamp(center + ivec2(x, y),
                                      ivec2(0), depthExtent - ivec2(1));
            sceneZ = max(sceneZ, texelFetch(sceneDepth, sampleCoord, 0).r);
        }
    }

    float bias = 1e-6 + 2.0 * fwidth(gl_FragCoord.z);
    if (gl_FragCoord.z > sceneZ + bias) {
        discard;
    }
    outMask = vec4(1.0);
}
