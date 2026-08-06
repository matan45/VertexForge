#ifndef TERRAIN_WEATHER_GLSL
#define TERRAIN_WEATHER_GLSL

// VK-1614 terrain local wetness / snow. CPU mirror: VFEngine/utilities/terrain/TerrainWeatherResponse.hpp.
//
// TERRAIN-ONLY, deliberately. common/wetness.glsl and common/snow_accumulation.glsl are NOT touched
// and NOT extended: they are shared with mesh_shader_gpudriven.glsl (static meshes), and the only way
// to prove static-mesh output is unperturbed is to leave their token stream byte-identical. The five
// duplicated statements in applyTerrainWetness below are the price of that proof; a test pins them
// against wetness.glsl so the duplication cannot drift.
//
// This file is included ONLY by mesh_terrain.glsl, and only under the weather macros. It is
// deliberately absent from terrain_rvt_bake.glsl: everything here runs AFTER the RVT-resolve /
// live-composite join, so baked pages stay weather-independent by construction rather than by
// discipline — one page serves every weather state, and no weather change invalidates the RVT.
//
// CONTRACT WITH THE INCLUDER (the same rule hex_tiling_terrain.glsl documents): these must already be
// declared at FILE scope, above this include —
//     tiles[]   terrainLayers[]   weightMapData[]   float sampleTileWeight(uint, uint, uint, vec2)

// A layer scalar is "authored" above this. An order of magnitude below the resolver's non-zero
// clamp floor (1/255), so a genuinely-authored value can never land on the wrong side of it.
const float TW_AUTHORED_EPS = 1e-6;

// Where standing water can form: cos(~21.5 deg) to cos(~10 deg) against world +Y.
const float TW_PUDDLE_SLOPE_MIN = 0.93;
const float TW_PUDDLE_SLOPE_MAX = 0.985;
// How much water pools on ground that is not concave at all. At 0 only crevices ever puddle (reads
// as noise); at 1 the concavity term is inert and flat ground floods uniformly.
const float TW_PUDDLE_OPENNESS = 0.25;
const vec3 TW_PUDDLE_TINT = vec3(0.5);
const float TW_PUDDLE_ROUGHNESS = 0.03;

struct TerrainWeatherResponse {
    float porosity;    // weighted mean over AUTHORED layers only
    float porosityT;   // authored coverage share, [0, 1]
    float retention;
    float retentionT;
};

// One 8-channel splat gather, run IDENTICALLY in the RVT-resolved and live-composite paths.
//
// That identity is the entire point. By the time weather is applied the per-layer identity is gone,
// and in the RVT-resolved path the composite loop never ran at all — so accumulating these inside
// the generated composite would need a SECOND implementation here for resolved fragments, and the
// two would have to agree bit-for-bit or the response steps along the page-residency boundary and
// crawls with the camera. One gather, reading set 1 bindings 0/1 (bound in BOTH paths), removes that
// failure class by construction. It also keeps the generated composite at 16 arms instead of 32.
//
// Cost: 8 x sampleTileWeight = 32 SSBO dword loads, but the 8 channels of a texel are 8 CONSECUTIVE
// bytes, so a bilinear over 4 texels touches ~8 distinct dwords (about one cache line). No texture
// fetches at all, against the composite's 16-64 textureGrad. Gated to exactly zero when off.
//
// Uses the RAW splat weights, never TERRAIN_HEIGHT_BLEND's sharpened `bw`: recovering bw needs each
// layer's ORM alpha (up to 8 extra textureGrad), and the RVT-resolved path has no per-layer ORM at
// all. Porosity and retention are low-frequency second-order shading inputs; raw coverage is the
// right approximation and the only one available in both paths.
TerrainWeatherResponse sampleTerrainWeatherResponse(uint tileIndex, vec2 tileUV)
{
    uint packedLI = floatBitsToUint(tiles[tileIndex].aabbMax.w);
    uint packedLI2 = floatBitsToUint(tiles[tileIndex].lodGeometricErrors2.z);
    uint wmOffset = tiles[tileIndex].weightMapOffset;
    uint wmRes = uint(tiles[tileIndex].aabbMin.w);

    float totalW = 0.0;
    float porSum = 0.0;
    float porAuth = 0.0;
    float retSum = 0.0;
    float retAuth = 0.0;

    for (int ch = 0; ch < 8; ch++) {
        uint packedWord = (ch < 4) ? packedLI : packedLI2;
        uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
        float w = sampleTileWeight(wmOffset, wmRes, uint(ch), tileUV);
        if (w < 0.001) continue;    // the composite's own cull, so coverage agrees with shading
        totalW += w;

        float p = terrainLayers[paletteIdx].layerPorosity;
        float r = terrainLayers[paletteIdx].layerSnowRetention;
        // 0.0 is the "did not opt in" sentinel. step() gives EXACTLY 0 for such a layer, which is
        // what makes the mix() at the call site collapse to a bitwise no-op. Unused palette slots
        // read a zero-initialised TerrainLayerGPUData and are therefore neutral too — palette
        // indices are allowed to exceed activeLayerCount.
        porSum += p * w;
        porAuth += step(TW_AUTHORED_EPS, p) * w;
        retSum += r * w;
        retAuth += step(TW_AUTHORED_EPS, r) * w;
    }

    float invW = 1.0 / max(totalW, 0.001);   // the composite's own guard

    TerrainWeatherResponse o;
    o.porosityT = porAuth * invW;
    o.retentionT = retAuth * invW;
    // max() keeps these finite when nothing was authored; the value is then multiplied by a mix
    // factor of exactly 0, so it never reaches the output.
    o.porosity = porSum / max(porAuth, TW_AUTHORED_EPS);
    o.retention = retSum / max(retAuth, TW_AUTHORED_EPS);
    return o;
}

// Probabilistic OR of a global weather scalar and a local contribution.
//
// Chosen over max(g, m) and clamp(g + m):
//   * max() makes a painted basin INVISIBLE while it rains (max(0.6, 0.3) == 0.6), which fails the
//     crevice-puddle half of the story outright;
//   * clamp(g + m) clips flat at the top, so a painted patch stops reading as wetter than its
//     surroundings exactly when the weather is most interesting;
//   * this form saturates BY CONSTRUCTION for inputs in [0, 1] — no clamp instruction, so nothing can
//     perturb the identity below — and is monotone non-decreasing in both arguments.
//
// mask == 0 returns `global` BITWISE (g + 0.0 * (1 - g) == g + 0.0 == g for finite g >= 0). That is
// exactly the property an absent or all-black default mask .vfImage depends on.
//
// Consequence, worth stating: a 0-neutral unsigned channel can only ADD. Suppression is delivered by
// the per-layer half instead — rock sheds snow because its layerSnowRetention is low, not because
// someone painted it.
float terrainWeatherOr(float global, float mask)
{
    return global + mask * (1.0 - global);
}

// common/wetness.glsl's four statements, verbatim, with the DERIVED porosity replaced by an argument.
// Therefore, for any x:
//     applyTerrainWetness(x, clamp(roughness * roughness, 0.0, 1.0), ...) == applyWetness(x, ...)
void applyTerrainWetness(float wetness, float porosity, inout vec3 albedo, inout float roughness,
                         inout float metallic, inout vec3 N)
{
    if (wetness < 0.001) return;

    albedo *= mix(1.0, 0.6, wetness * porosity);
    roughness = mix(roughness, roughness * 0.3, wetness);
    metallic = mix(metallic, max(metallic, 0.02), wetness);
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetness * 0.3));
}

// Standing water ON TOP of the (already wet) substrate.
//
// Separate from applyTerrainWetness because they are different surfaces, not different intensities of
// one surface: at wetness 1 the wetness response yields roughness ~0.24 with the normal flattened
// 30%, which reads as damp soil. Water is a near-mirror dielectric with a FLAT normal. Driving
// wetness to 1 does not produce a puddle, which is why the AC's three symptoms need their own term.
void applyTerrainPuddle(float puddleT, inout vec3 albedo, inout float roughness,
                        inout float metallic, inout vec3 N)
{
    if (puddleT < 0.001) return;    // fires on every sloped fragment

    albedo = mix(albedo, albedo * TW_PUDDLE_TINT, puddleT);
    roughness = mix(roughness, TW_PUDDLE_ROUGHNESS, puddleT);
    metallic = mix(metallic, 0.0, puddleT);
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), puddleT));
}

// Where water pools: flat, concave, non-absorbent ground. Every input is already in a register at the
// apply point, so this costs no fetch.
//   upDotY   - dot(normalize(GEOMETRIC world normal), +Y), so a detail normal map cannot move a puddle
//   ao       - live in BOTH paths (rvtO.r resolved, ls_AO in the fallback), making (1 - ao) a free
//              concavity signal — the "crevice" term the UE5 bar refers to
//   porosity - the blended per-layer value: sand drinks the water instead of pooling it. This is what
//              makes the per-layer half of the story load-bearing rather than a spare knob.
// All four factors are in [0, 1], so the product is too and needs no clamp. Monotone increasing in
// wetness, flatness and concavity; monotone decreasing in porosity.
float terrainPuddleCoverage(float wet, float upDotY, float ao, float porosity)
{
    float flatness = smoothstep(TW_PUDDLE_SLOPE_MIN, TW_PUDDLE_SLOPE_MAX, upDotY);
    float cavity = mix(1.0 - ao, 1.0, TW_PUDDLE_OPENNESS);
    return wet * flatness * cavity * (1.0 - porosity);
}

#endif // TERRAIN_WEATHER_GLSL
