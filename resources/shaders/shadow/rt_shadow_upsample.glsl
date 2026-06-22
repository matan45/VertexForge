#type COMPUTE
#version 460

// VK-1430: edge-aware joint-bilateral upsample of the half-resolution RT shadow mask back to full
// resolution. Each full-res pixel gathers the 4 nearest half-res denoised taps and weights them by
// bilinear footprint * depth similarity * normal similarity, so the half-res mask reconstructs
// crisply across depth/normal discontinuities instead of bleeding (which a naive bilinear filter
// would do). Falls back to the nearest tap when every weight collapses (disocclusion edges), which
// prevents NaN from a zero-sum normalize.
//
// Shared by the directional (RTShadowUpsamplePipeline) and the spot/point layered upsamplers
// (RTLayeredShadowUpsamplePipeline): the layered variant binds a single half-res mask layer (e2D)
// and a single full-res output layer (e2D) per slice dispatch — mirroring how RTLayeredShadowDenoiser
// reuses the 2D denoise shaders per layer — so one plain 2D shader serves both.
//
// Guide design: there is NO half-res depth/normal image — the half-res trace samples the FULL-res
// depth/normal directly at its half-texel-center UV (see rt_shadow.glsl), so the full-res guide IS
// the half-res guide. We therefore sample the full-res depth/normal both at the full pixel center
// and at each half-tap center; the half-tap-center samples reproduce the exact depth/normal each
// half-res ray used. This needs zero extra images.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: inputs (half-res denoised mask + full-res depth/normal guides) and full-res output.
layout(set = 0, binding = 0) uniform sampler2D halfMask;        // half-res denoised mask (one slice)
layout(set = 0, binding = 1) uniform sampler2D depthBuffer;     // full-res depth
layout(set = 0, binding = 2) uniform sampler2D normalBuffer;    // full-res world-space normal
layout(set = 0, binding = 3, r16f) uniform writeonly image2D outMask; // full-res output (one slice)

layout(push_constant) uniform PushConstants {
    uvec2 dstExtent;      // full-res output dimensions (width, height)
    vec2  invHalfDims;    // 1.0 / half-res mask dimensions
    vec2  invFullDims;    // 1.0 / full-res dimensions
    float depthThreshold; // larger => more depth tolerance
    float normalExp;      // pow() exponent for normal edge-stopping
    int   _reserved0;     // padding to keep the 40-byte layout shared by both upsample pipelines
    int   _reserved1;
};

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= int(dstExtent.x) || pixel.y >= int(dstExtent.y))
        return;

    // Full-res sample point.
    vec2 uvFull = (vec2(pixel) + 0.5) * invFullDims;
    float centerDepth = texture(depthBuffer, uvFull).r;

    // Sky: pass the nearest tap straight through (no surface to edge-stop against).
    if (centerDepth >= 1.0 || centerDepth <= 0.0) {
        imageStore(outMask, pixel, vec4(texture(halfMask, uvFull).r));
        return;
    }

    vec3 centerNormal = normalize(texture(normalBuffer, uvFull).xyz);

    // Locate the full pixel within the half-res grid (continuous half-texel coordinate), then the
    // 4 surrounding half-res texel centers and their bilinear fractions.
    vec2 halfCoord = uvFull / invHalfDims - 0.5;
    vec2 baseTexel = floor(halfCoord);
    vec2 frac = halfCoord - baseTexel;

    float sumWeight = 0.0;
    float sumShadow = 0.0;
    float nearestShadow = 0.0;
    float nearestWeight = -1.0; // tracks the largest bilinear footprint for the fallback tap

    for (int dy = 0; dy <= 1; ++dy) {
        for (int dx = 0; dx <= 1; ++dx) {
            vec2 tapTexel = baseTexel + vec2(dx, dy);
            vec2 tapUV = (tapTexel + 0.5) * invHalfDims; // half-res texel center in [0,1]

            float bilinear = (dx == 0 ? (1.0 - frac.x) : frac.x) *
                             (dy == 0 ? (1.0 - frac.y) : frac.y);

            float tapShadow = texture(halfMask, tapUV).r;
            // Guide read at the half-tap center: full-res depth/normal reproduce what the half-res
            // ray used (it sampled the full-res buffers at exactly this UV).
            float tapDepth = texture(depthBuffer, tapUV).r;
            vec3  tapNormal = normalize(texture(normalBuffer, tapUV).xyz);

            float wDepth = exp(-abs(centerDepth - tapDepth) / (depthThreshold + 1e-6));
            float wNormal = pow(max(dot(centerNormal, tapNormal), 0.0), normalExp);

            float w = bilinear * wDepth * wNormal;
            sumShadow += tapShadow * w;
            sumWeight += w;

            if (bilinear > nearestWeight) {
                nearestWeight = bilinear;
                nearestShadow = tapShadow;
            }
        }
    }

    // Weight floor + nearest-tap fallback guard against a degenerate (all-rejected) gather.
    float result = (sumWeight > 1e-5) ? (sumShadow / sumWeight) : nearestShadow;
    imageStore(outMask, pixel, vec4(result));
}
