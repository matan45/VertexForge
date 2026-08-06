#ifndef TERRAIN_PARALLAX_GLSL
#define TERRAIN_PARALLAX_GLSL

// VK-1625 terrain parallax from composited height (POM-lite).
// CPU mirror: VFEngine/utilities/terrain/TerrainParallax.hpp.
//
// VK-1609 put a real per-layer height in ORM alpha so the composite could sharpen its splat weights.
// This spends that same height a second time, as a view-dependent offset on the ONE base UV every
// layer derives from (terrain_material_generated.glsl: `layerUV = triplanarWorldUV * tilingScale`).
// Offsetting that single UV before the composite is #include-d gives all eight layers a
// world-consistent parallax WITHOUT the generated snippet changing by a token — no fifth permutation
// flag, no 32-arm chain, no golden-string regeneration.
//
// LIVE PATH ONLY, and deliberately absent from terrain_rvt_bake.glsl. The bake is orthographic,
// top-down and binds no camera, so a view-dependent offset is undefined there. Instead the offset is
// applied at FINAL SHADING to BOTH consumers of the surface position — the RVT lookup UV and the live
// composite's base UV — so baked pages stay view-independent (nothing about the bake changes, not one
// byte, and no resident page is invalidated) while resolved and fallback fragments still agree across
// a page-residency boundary. That last property is the failure class VK-1611 and VK-1614 both had to
// design around; here it falls out of there being a single offset rather than two implementations.
//
// CONTRACT WITH THE INCLUDER (same rule terrain_weather.glsl and hex_tiling_terrain.glsl document):
// these must already be declared at FILE scope, above this include —
//     tiles[]   terrainLayers[]   bindlessTextures[]   float sampleTileWeight(uint, uint, uint, vec2)
// and the caller must pass screen-space gradients taken in UNIFORM control flow, because every fetch
// below runs inside the divergent `continue` of the splat loop.

// The composite's own weight cull. Sharing the constant is what makes the marched height field agree
// with the surface that is finally shaded: the march visits exactly the layers the composite will.
const float TP_WEIGHT_CULL = 0.001;

// Grazing-angle guards. The offset carries a 1/dot(V,N); the clamp keeps it bounded even for one
// frame, and the fade takes it to zero over the band above it. A bare clamp would leave a visible
// crease along the iso-line where it starts — the same failure VK-1609's smoothstep exists to avoid.
//
// TP_MIN_NDOTV <= TP_GRAZE_FADE_MIN is the invariant that makes the pair work: the clamp may only
// engage where the fade has ALREADY reached exactly zero, so no angle is ever both clamped and
// visible. Mirrored (and static_assert-ed) in TerrainParallax.hpp.
const float TP_MIN_NDOTV = 0.10;
const float TP_GRAZE_FADE_MIN = 0.10;
const float TP_GRAZE_FADE_MAX = 0.30;

// The per-fragment splat gather, done ONCE and reused by every march step.
//
// Splat weights are FROZEN at the fragment's own tile UV for the whole march. That is not an
// approximation being tolerated, it is the right model: weights vary at splat-map scale (metres)
// while the offset is centimetres, and re-sampling them per step would cost 8 more SSBO gathers per
// step to move the answer by nothing. It also keeps the marched field consistent with the composite,
// which is likewise evaluated at the unoffset fragTexCoord.
//
// Arrays are a fixed 8 and the consumer loops 0..7 with the composite's `continue`, so the bound is a
// compile-time constant and the whole thing stays in registers instead of spilling to scratch.
struct TerrainParallaxField {
    float weight[8];
    uint  ormIdx[8];
    float tiling[8];
    float invTotalW;
};

TerrainParallaxField terrainParallaxGather(uint tileIndex, vec2 tileUV)
{
    uint packedLI = floatBitsToUint(tiles[tileIndex].aabbMax.w);
    uint packedLI2 = floatBitsToUint(tiles[tileIndex].lodGeometricErrors2.z);
    uint wmOffset = tiles[tileIndex].weightMapOffset;
    uint wmRes = uint(tiles[tileIndex].aabbMin.w);

    TerrainParallaxField f;
    float totalW = 0.0;
    for (int ch = 0; ch < 8; ch++) {
        uint packedWord = (ch < 4) ? packedLI : packedLI2;
        uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
        float w = sampleTileWeight(wmOffset, wmRes, uint(ch), tileUV);
        f.weight[ch] = w;
        f.ormIdx[ch] = terrainLayers[paletteIdx].ormTextureIndex;
        f.tiling[ch] = terrainLayers[paletteIdx].tilingScale;
        // Culled channels must not enter the denominator either, or the normalised height would be
        // biased low wherever a sliver layer exists — and a height field biased low is a uniform
        // sink, i.e. exactly the swim artefact the reference plane exists to remove.
        totalW += (w < TP_WEIGHT_CULL) ? 0.0 : w;
    }
    f.invTotalW = 1.0 / max(totalW, TP_WEIGHT_CULL); // the composite's own guard
    return f;
}

// The composited height at one point along the ray, in [0,1] with 1 = the geometric surface.
//
// RAW splat weights, never TERRAIN_HEIGHT_BLEND's sharpened `bw` — the same call VK-1614's weather
// gather makes, and for a stronger reason here: `bw` is a function of the very ORM alpha this is
// trying to find.
//
// A layer with no ORM contributes exactly 1.0, the top plane, so it displaces nothing. Combined with
// the reference remap that is what makes unauthored content free: every ORM packed before VK-1609 has
// its alpha force-written to 255, so h == 1 everywhere, the ray meets the surface at depth 0, and the
// offset is bitwise vec2(0).
//
// Gradients are the BASE screen-space gradients, unscaled by the offset — standard practice, and
// required here because they were taken in uniform control flow before the divergent branch below.
// Hex-tiling layers are sampled with a plain tap rather than the 3-tap hex blend: the march's job is
// to find a depth, not to reproduce the composite bit for bit, and tripling its fetch count to do so
// would not be visible.
float terrainParallaxHeight(TerrainParallaxField f, vec2 uv, vec2 uvdx, vec2 uvdy, float invRefHeight)
{
    float h = 0.0;
    for (int ch = 0; ch < 8; ch++) {
        float w = f.weight[ch];
        if (w < TP_WEIGHT_CULL) continue;
        uint ormIdx = f.ormIdx[ch];
        float hLayer = 1.0;
        if (ormIdx > 0u) {
            float s = f.tiling[ch];
            float a = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], uv * s, uvdx * s, uvdy * s).a;
            // invRefHeight is >= 1 by construction (referenceHeight is clamped into (0,1] on upload),
            // and it is exactly 1.0 at the default, where this is the bitwise identity for any a in
            // [0,1]. See TerrainParallax.hpp for why the knob has to exist at all: an ORM packed after
            // VK-1609 with no height input carries a flat 0.5 alpha, which height blending cannot see
            // but parallax would render as a swimming surface.
            hLayer = clamp(a * invRefHeight, 0.0, 1.0);
        }
        h += w * hLayer;
    }
    return h * f.invTotalW;
}

// 1 inside fadeStart, 0 beyond fadeEnd.
//
// Keyed on CAMERA DISTANCE, not on terrainFootprintLog2. VK-1611 chose footprint space for its own
// fade because that effect also runs in the camera-less bake and the two had to agree; parallax never
// runs in the bake, so the constraint that forced footprint space does not apply and the
// artist-meaningful unit wins. The divergence is deliberate, not an oversight.
float terrainParallaxDistanceFade(float cameraDistance, float fadeStart, float fadeEnd)
{
    return 1.0 - smoothstep(fadeStart, fadeEnd, cameraDistance);
}

float terrainParallaxGrazeFade(float nDotV)
{
    return smoothstep(TP_GRAZE_FADE_MIN, TP_GRAZE_FADE_MAX, nDotV);
}

// World-space offset per metre of depth below the surface, with depth measured ALONG the geometric
// normal — which is what makes this correct on a slope and not only on flat ground.
//
// V points surface -> eye, so -V goes into the surface. The point z below the surface along the view
// ray is P - V*z/dot(V,N): substituting back gives dot(P - Q, N) == z exactly. No tangent basis is
// needed anywhere, which also means nothing here can fall out of sync with the normal-mapping block
// further down mesh_terrain.glsl.
vec3 terrainParallaxRayStep(vec3 V, vec3 N)
{
    return -V / max(dot(V, N), TP_MIN_NDOTV);
}

// Solve for the depth at which the view ray meets the composited height field.
//
// Steep-parallax linear search plus one secant refinement, in BRANCHLESS form: the loop runs its full
// uniform iteration count with no early exit and no `break`, and the first crossing is captured
// arithmetically through `crossed`, which is 1.0 exactly once. That is what satisfies the AC's "fixed
// iteration count (uniform control flow)" — breaking out on a per-fragment condition would put the
// ORM fetches inside divergent flow and cost more in reconvergence than the skipped steps save.
//
// `uvStep` is the base-UV offset per metre of depth. Because the UV is an affine function of world
// position, the projection is linear and can be hoisted entirely out of the loop: the UV at depth z is
// just baseUV + uvStep*z. That is why the inner loop contains texture work and nothing else.
//
// CONTRACT, asserted by test_terrain_parallax.cpp: for a field that is constant at 1 the surface depth
// is 0 at every sample, the first iteration crosses, the secant weight is exactly 0, and this returns
// exactly 0.0 — so `baseUV += uvStep * 0.0` cannot perturb a bit.
float terrainParallaxSolveDepth(TerrainParallaxField f, vec2 baseUV, vec2 uvdx, vec2 uvdy,
                                vec2 uvStep, float depth, uint steps, float invRefHeight)
{
    // The resolver clamps this into [1,32]; re-clamped here only so a zeroed dummy UBO cannot produce
    // a full-depth offset from an empty search. Costs one instruction inside an already-gated block.
    uint n = max(steps, 1u);
    float layerStep = depth / float(n);

    // Step 0 sits on the geometric surface. It is never a crossing candidate on its own — it is only
    // ever the `prev` half of the first bracket.
    float zPrev = 0.0;
    float sdPrev = depth * (1.0 - terrainParallaxHeight(f, baseUV, uvdx, uvdy, invRefHeight));

    float hitZ0 = 0.0, hitSd0 = 0.0, hitZ1 = 0.0, hitSd1 = 0.0, found = 0.0;
    for (uint i = 1u; i <= n; i++) {
        float z = float(i) * layerStep;
        float sd = depth * (1.0 - terrainParallaxHeight(f, baseUV + uvStep * z, uvdx, uvdy, invRefHeight));
        float below = step(sd, z);
        float crossed = below * (1.0 - found);
        hitZ0 += crossed * zPrev;
        hitSd0 += crossed * sdPrev;
        hitZ1 += crossed * z;
        hitSd1 += crossed * sd;
        found = max(found, below);
        zPrev = z;
        sdPrev = sd;
    }

    // Secant across the bracket: solve (z - sd) == 0. The denominator is the bracket width plus the
    // surface's own change over it, and it is only degenerate when the bracket is empty, which `found`
    // already covers.
    float d0 = hitSd0 - hitZ0;
    float d1 = hitSd1 - hitZ1;
    float denom = d0 - d1;
    float t = clamp((abs(denom) > 1e-8) ? d0 / denom : 0.0, 0.0, 1.0);
    float zStar = mix(hitZ0, hitZ1, t);
    // No crossing anywhere in the volume means the ray left through the bottom; the deepest point is
    // the conventional clamp. mix with a 0/1 selector is exact in both directions.
    return mix(depth, zStar, found);
}

#endif // TERRAIN_PARALLAX_GLSL
