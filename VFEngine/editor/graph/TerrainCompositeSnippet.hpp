#pragma once
#include <string>

// Emitter for the terrain splat-composite GLSL snippet that ShaderGraphCompiler writes to
// resources/shaders/material/terrain_material_generated.glsl. That single generated file is
// #include-d verbatim by BOTH the live terrain fragment shader (mesh_terrain.glsl, in the
// non-RVT path and in the RVT fallback branch) and the RVT bake shader
// (terrain_rvt_bake.glsl) — so the per-layer logic can only be edited in one place and the
// baked pages can never disagree with the live composite.
//
// This lives in a header, outside ShaderGraphCompiler.cpp, purely so the Tests project can
// call it: Tests has VFEngine/editor on its include path but does NOT link Editor, so the
// emitted text is only reachable from a test if it is inline. test_terrain_height_blend.cpp
// pins the linear (non-height-blend) arms against a golden string — that is what makes
// VK-1609's "bit-identical fallback" claim a build failure rather than a belief.
//
// CONTRACT WITH THE INCLUDER. All of these must be defined, in UNIFORM control flow, before
// the #include (the snippet is included INSIDE main(), so it can declare no functions of its
// own — every helper it calls comes from a file-scope include in mesh_terrain.glsl and
// terrain_rvt_bake.glsl):
//   tiles[], terrainLayers[], terrainAntiTiling, bindlessTextures[], sampleTileWeight()
//   fragTileIndex, fragTexCoord
//   triplanarWorldUV, triplanarWorldUVdx, triplanarWorldUVdy
//   terrainWorldXZ           world-space XZ, UNSCALED (VK-1611 macro variation is world-anchored,
//                            and triplanarWorldUV is already multiplied by textureScale and is
//                            triplanar-blended inside caves, so it cannot serve)
//   terrainFootprintLog2     log2(texture repeats per output texel) of the BASE UV, i.e.
//                            0.5 * log2(max(dot(dx,dx), dot(dy,dy))). See TerrainAntiTiling.hpp
//                            for why this, and not camera distance, is the distance signal.

namespace editor::graph
{
    // Emits the per-tile terrain composite loop for one shader permutation. All permutations
    // share a single body here so the per-layer logic (weight sampling, ORM unpack, tiling /
    // gradient math) can only be edited in one place.
    //   `detail`          inserts the extra work the TERRAIN_DETAIL_MAPS permutation samples — the
    //                     normal-map fetch, the emission-texture path, and the mat_normalTS output.
    //   `heightBlend`     inserts the VK-1609 TERRAIN_HEIGHT_BLEND weight sharpening.
    //   `distanceRescale` inserts the VK-1611 TERRAIN_DISTANCE_RESCALE second albedo tap.
    //   `hexTiling`       inserts the VK-1612 TERRAIN_HEX_TILING 3-tap stochastic sampling.
    // The last two default to false so that every pre-existing two-argument call — including the
    // golden-string tests — returns the identical text it always did. With all three optional
    // flags false the output is byte-identical to the pre-VK-1609 emitter: the accumulation
    // operand is the literal `w`, the ORM sample stays a vec3, and not one extra token is
    // emitted. That is bit-identity by construction rather than by inspection, and it is what
    // makes each of these features provably free for projects that do not use it.
    inline std::string buildTerrainCompositeLoop(bool detail, bool heightBlend,
                                                 bool distanceRescale = false, bool hexTiling = false)
    {
        // Naming the accumulation operand once is what keeps the two arms in lockstep. The two
        // emission accumulators below sit inside an `if (emissionIdx > 0u)` block, visually
        // separated from the main accumulation group; if they kept `* w` while ls_TotalW
        // accumulated the sharpened weight, terrain emission would be normalized by the wrong
        // denominator and silently drift with height.
        const std::string W = heightBlend ? "bw" : "w";

        // VK-1612: one layer fetch, as either the single textureGrad the composite has always
        // used or a hex 3-tap blend selected per layer. Emitting it through these two helpers is
        // what guarantees the non-hex text is unchanged character for character — the `plain`
        // string below IS the original expression, and when hexTiling is false nothing wraps it.
        //
        // The hex selector is a ternary, not an `if`, deliberately: GLSL evaluates only the taken
        // operand, so a layer that opted out pays one compare rather than three texture fetches,
        // and the composite's `if (` count is unchanged — the "no new branch" property the
        // explicit-gradient (textureGrad) contract rests on.
        auto albedoFetch = [&](const char* uv, const char* dx, const char* dy, const char* blend)
        {
            const std::string plain = std::string("textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], ")
                                    + uv + ", " + dx + ", " + dy + ").rgb";
            if (!hexTiling)
                return plain;
            return "((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, " + std::string(blend)
                 + ") : " + plain + ")";
        };
        auto normalFetch = [&](const char* uv, const char* dx, const char* dy, const char* blend)
        {
            const std::string plain = std::string("textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], ")
                                    + uv + ", " + dx + ", " + dy + ").xyz * 2.0 - 1.0";
            if (!hexTiling)
                return plain;
            return "((hexStrength > 0.0) ? hexTerrainSampleNormal(normalIdx, " + std::string(blend)
                 + ") : " + plain + ")";
        };

        std::string s;
        s += "vec3 ls_Albedo = vec3(0.0);\n";
        if (detail) s += "vec3 ls_Normal = vec3(0.0);\n";
        s += "float ls_Roughness = 0.0;\n";
        s += "float ls_Metallic = 0.0;\n";
        s += "float ls_AO = 0.0;\n";
        s += "float ls_Emission = 0.0;\n";
        if (detail)
        {
            s += "float ls_EmissionScalar = 0.0;\n";
            s += "vec3 ls_EmissionColor = vec3(0.0);\n";
        }
        s += "float ls_TotalW = 0.0;\n";
        s += "uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);\n";
        s += "uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);\n";
        // Skip channels that carry no weight anywhere on this tile. The mask is computed at
        // weight-map upload (computeUsedWeightChannelMask) and rides lodGeometricErrors2.w; the
        // cull sits BEFORE sampleTileWeight on purpose — that is where the 4 SSBO loads per
        // unused channel disappear. 0 means "no mask": old tile data degrades to looping all 8.
        s += "uint usedChMask = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.w);\n";
        s += "if (usedChMask == 0u) { usedChMask = 0xFFu; }\n";
        s += "for (int ch = 0; ch < 8; ch++) {\n";
        s += "    if ((usedChMask & (1u << uint(ch))) == 0u) continue;\n";
        s += "    uint packedWord = (ch < 4) ? packedLI : packedLI2;\n";
        s += "    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;\n";
        s += "    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, "
             "uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);\n";
        // The cull deliberately tests the RAW splat weight, never the height-sharpened one: a
        // layer the artist genuinely painted must not be dropped because its height biased it
        // small. See the smoothstep below for how the sharpening is faded out to meet it.
        s += "    if (w < 0.001) continue;\n";
        // Layer samples run inside per-fragment-divergent control flow (this `continue`, and
        // mesh_terrain's RVT resolved/fallback branch), where implicit-LOD texture() derivatives
        // are undefined and cause mip shimmer at RVT seams — so sample with EXPLICIT gradients
        // (textureGrad). The includer must define triplanarWorldUVdx/dy (screen-space gradients
        // of triplanarWorldUV) in uniform control flow before including this snippet.
        if (detail)
            s += "    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.\n";
        s += "    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;\n";
        s += "    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;\n";
        s += "    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;\n";
        if (distanceRescale)
        {
            // VK-1611: the layer's own footprint is the base footprint shifted by
            // log2(tilingScale) — exact, because layerUVdx == triplanarWorldUVdx * tilingScale.
            // That is why ONE material-global knee is correct across layers that tile at wildly
            // different rates: the knee lives in footprint space, not in world space.
            s += "    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);\n";
            s += "    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, "
                 "terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) "
                 "* terrainAntiTiling.rescaleStrength;\n";
            s += "    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;\n";
            s += "    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;\n";
            s += "    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;\n";
        }
        if (hexTiling)
        {
            // resolveTerrainLayerPBR uploads 0 for every layer that did not opt in, so this is
            // exactly "does this layer hex-tile". The lattice math below is pure ALU (~35 ops);
            // only the three texture taps are gated, which is where the cost actually is.
            s += "    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;\n";
            s += "    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, "
                 "terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, "
                 "terrainLayers[paletteIdx].hexRotationStrength);\n";
            if (distanceRescale)
            {
                // The far tap must hex-tile too when the layer does. Mixing a hex-sampled near
                // tap toward a PLAIN far tap would make the layer revert to visibly repeating
                // sampling exactly where repetition is worst — the opposite of the intent.
                s += "    HexTerrainBlend hbFar = hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy, "
                     "terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, "
                     "terrainLayers[paletteIdx].hexRotationStrength);\n";
            }
        }
        s += "    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;\n";
        s += "    vec3 layerAlbedo = (albedoIdx > 0u) ? "
             + albedoFetch("layerUV", "layerUVdx", "layerUVdy", "hb") + " : vec3(0.5);\n";
        if (distanceRescale)
        {
            s += "    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? "
                 + albedoFetch("layerFarUV", "layerFarUVdx", "layerFarUVdy", "hbFar")
                 + " : vec3(0.5), lsFarT);\n";
        }
        // The non-detail permutation lights terrain with the geometric normal only (mesh_terrain
        // uses N = normalize(fragNormal); the RVT bake writes no normal plane), so it omits the
        // per-layer normal fetch — a composited tangent-space normal would be dead work there.
        if (detail)
        {
            s += "    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;\n";
            s += "    vec3 layerNormal = (normalIdx > 0u) ? "
                 + normalFetch("layerUV", "layerUVdx", "layerUVdy", "hb") + " : vec3(0.0, 0.0, 1.0);\n";
        }
        s += "    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;\n";
        if (heightBlend)
        {
            // VK-1609: widening the existing ORM fetch from .rgb to .rgba is the SAME sample
            // instruction — alpha was already in the returned register and thrown away.
            s += "    float layerAO, layerRoughness, layerMetallic, layerHeight;\n";
            s += "    if (ormIdx > 0u) {\n";
            s += "        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);\n";
            s += "        layerAO = ormSample.r;\n";
            s += "        layerRoughness = ormSample.g;\n";
            s += "        layerMetallic = ormSample.b;\n";
            s += "        layerHeight = ormSample.a;\n";
            s += "    } else {\n";
            s += "        layerAO = terrainLayers[paletteIdx].ao;\n";
            s += "        layerRoughness = terrainLayers[paletteIdx].roughness;\n";
            s += "        layerMetallic = terrainLayers[paletteIdx].metallic;\n";
            s += "        layerHeight = 0.5;\n";
            s += "    }\n";
            // Branchless single-pass weight sharpening: the splat weight is biased by an
            // exponential of the layer's packed height. No new branch, so the textureGrad
            // explicit-gradient contract above is untouched, and no extra texture fetch.
            //
            // heightBlendContrast == 0 => mix() returns EXACTLY 1.0 (IEEE-754: 1.0*(1-0) + y*0
            // == 1.0 for finite y) and w * 1.0 == w bitwise, i.e. the legacy linear composite.
            // The resolver uploads 0 for every layer with no height source, so those layers are
            // bit-identical even inside this permutation. The [0, 16] clamp it applies is a
            // correctness invariant, not a nicety: an infinity here would make 0.0 * y == NaN.
            //
            // The smoothstep ramps the sharpening to zero at the `w < 0.001` cull above. Without
            // it, a layer at w~0.001 with high height contributes ~25% at contrast 8 and then
            // vanishes at the cull, drawing a hard contour ring along the w=0.001 iso-line
            // (weights are 8-bit and bilerped, so that band is world-space large).
            s += "    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;\n";
            s += "    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);\n";
            s += "    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);\n";
        }
        else
        {
            s += "    float layerAO, layerRoughness, layerMetallic;\n";
            s += "    if (ormIdx > 0u) {\n";
            s += "        vec3 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy).rgb;\n";
            s += "        layerAO = ormSample.r;\n";
            s += "        layerRoughness = ormSample.g;\n";
            s += "        layerMetallic = ormSample.b;\n";
            s += "    } else {\n";
            s += "        layerAO = terrainLayers[paletteIdx].ao;\n";
            s += "        layerRoughness = terrainLayers[paletteIdx].roughness;\n";
            s += "        layerMetallic = terrainLayers[paletteIdx].metallic;\n";
            s += "    }\n";
        }
        s += "    float layerEmission = terrainLayers[paletteIdx].emissionStrength;\n";
        if (detail)
        {
            s += "    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;\n";
            s += "    if (emissionIdx > 0u) {\n";
            s += "        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;\n";
            s += "        ls_EmissionColor += emissionSample * layerEmission * " + W + ";\n";
            s += "    } else {\n";
            s += "        ls_EmissionScalar += layerEmission * " + W + ";\n";
            s += "    }\n";
        }
        s += "    ls_Albedo += layerAlbedo * " + W + ";\n";
        if (detail) s += "    ls_Normal += layerNormal * " + W + ";\n";
        s += "    ls_Roughness += layerRoughness * " + W + ";\n";
        s += "    ls_Metallic += layerMetallic * " + W + ";\n";
        s += "    ls_AO += layerAO * " + W + ";\n";
        s += "    ls_Emission += layerEmission * " + W + ";\n";
        s += "    ls_TotalW += " + W + ";\n";
        s += "}\n";
        s += "float ls_InvW = 1.0 / max(ls_TotalW, 0.001);\n";
        s += "ls_Albedo *= ls_InvW;\n";
        if (detail) s += "ls_Normal *= ls_InvW;\n";
        s += "ls_Roughness *= ls_InvW;\n";
        s += "ls_Metallic *= ls_InvW;\n";
        s += "ls_AO *= ls_InvW;\n";
        s += "ls_Emission *= ls_InvW;\n";
        if (detail)
        {
            s += "ls_EmissionScalar *= ls_InvW;\n";
            s += "ls_EmissionColor *= ls_InvW;\n";
        }
        s += "// Terrain material properties\n";
        s += "vec3 mat_albedo = ls_Albedo;\n";
        if (detail)
        {
            s += "#define MAT_NORMALTS_DEFINED\n";
            s += "float ls_NormalLengthSq = dot(ls_Normal, ls_Normal);\n";
            s += "vec3 mat_normalTS = (ls_NormalLengthSq > 1e-8) ? ls_Normal * inversesqrt(ls_NormalLengthSq) : vec3(0.0, 0.0, 1.0);\n";
        }
        s += "float mat_metallic = ls_Metallic;\n";
        s += "float mat_roughness = ls_Roughness;\n";
        s += "float mat_ao = ls_AO;\n";
        s += "#define MAT_EMISSION_DEFINED\n";
        if (detail)
            s += "vec3 mat_emission = mat_albedo * ls_EmissionScalar + ls_EmissionColor;\n";
        else
            s += "vec3 mat_emission = mat_albedo * ls_Emission;\n";
        return s;
    }

    // VK-1611 world-anchored macro variation, emitted ONCE after the permutation nest rather than
    // inside every arm. Three things fall out of that placement, and all three are load-bearing:
    //
    //  1. buildTerrainCompositeLoop() is untouched, so the golden strings that pin VK-1609's
    //     byte-identical linear arms need no regeneration.
    //  2. It runs after each arm has already computed mat_emission from mat_albedo, so emission
    //     is NOT modulated. Macro variation is a reflectance tint; emission is a light source.
    //  3. One evaluation per fragment instead of one per active splat channel.
    //
    // It carries its own macro, and — because it sits OUTSIDE the arm nest — that macro costs one
    // extra #ifdef rather than doubling the 16 arms. Worth it: measured against the pre-VK-1611
    // tree with glslc -O, the ungated tail added 4032 bytes of SPIR-V to EVERY permutation
    // (~200 ALU: two octaves x four 32-bit hashes plus the interpolation). At strength 0 the
    // multiplier is still exactly 1.0, so leaving it ungated would have been correct — but
    // "correct and free" beats "correct and 200 ALU on every terrain fragment that misses the RVT
    // page cache", and this repo's other permutations all hold that line.
    //
    // Anchored to terrainWorldXZ, NOT to any layer UV, so the pattern is fixed in the world and
    // identical in the bake (which has no camera) and live. Deliberately NOT slope-masked the way
    // Godot Terrain3D masks its equivalent: the RVT bake is a flat top-down pass with no surface
    // normal, so a slope term would make baked pages disagree with the live fallback.
    inline std::string buildTerrainMacroVariationSnippet()
    {
        std::string s;
        s += "// VK-1611 world-anchored macro variation (identity at strength 0).\n";
        s += "float tmv_n0 = terrainValueNoise2D(terrainWorldXZ * terrainAntiTiling.macroFrequency0, "
             "terrainAntiTiling.macroSeed);\n";
        s += "float tmv_n1 = terrainValueNoise2D(terrainWorldXZ * terrainAntiTiling.macroFrequency1, "
             "terrainAntiTiling.macroSeed ^ 0x9E3779B9u);\n";
        s += "mat_albedo *= (1.0 + terrainAntiTiling.macroStrength * (tmv_n0 - 0.5)) "
             "* (1.0 + terrainAntiTiling.macroStrength * (tmv_n1 - 0.5));\n";
        return s;
    }

    // The preprocessor assembly written to terrain_material_generated.glsl, without the leading
    // provenance comments (those carry the material's layer count and so are emitted by
    // ShaderGraphCompiler::compileTerrainMaterial). Keeping the assembly here means the golden
    // test can assert the checked-in file ends with exactly this text.
    //
    // Four independent permutation macros = 16 arms. They are emitted as a FLAT #if / #elif chain
    // in which every arm names all four macros explicitly (`defined` or `!defined`), rather than
    // as a four-deep #ifdef nest: the conditions are then exhaustive and mutually exclusive by
    // construction, and adding a fifth flag is a one-line change here instead of a re-indent of
    // the whole file. Only one arm survives preprocessing, so shaderc's cost is unchanged.
    inline std::string buildTerrainCompositeSnippet()
    {
        struct Flag { const char* macro; };
        // Bit order must stay stable: it decides the emitted arm order and the goldens' position.
        static constexpr Flag FLAGS[] = {
            {"TERRAIN_DETAIL_MAPS"},      // bit 0
            {"TERRAIN_HEIGHT_BLEND"},     // bit 1
            {"TERRAIN_DISTANCE_RESCALE"}, // bit 2
            {"TERRAIN_HEX_TILING"},       // bit 3
        };
        constexpr unsigned FLAG_COUNT = 4;
        constexpr unsigned ARM_COUNT = 1u << FLAG_COUNT;

        auto condition = [](unsigned mask)
        {
            std::string c;
            for (unsigned bit = 0; bit < FLAG_COUNT; ++bit)
            {
                if (bit > 0) c += " && ";
                if ((mask & (1u << bit)) == 0) c += "!";
                c += "defined(";
                c += FLAGS[bit].macro;
                c += ")";
            }
            return c;
        };

        std::string code;
        // Descending so that mask 0 — every feature off, the shader most projects compile — is
        // last and can be the #else, which makes the chain total: no combination can fall
        // through and leave mat_albedo undeclared.
        for (unsigned mask = ARM_COUNT - 1;; --mask)
        {
            if (mask == ARM_COUNT - 1)
                code += "#if " + condition(mask) + "\n";
            else if (mask > 0)
                code += "#elif " + condition(mask) + "\n";
            else
                code += "#else\n";

            code += buildTerrainCompositeLoop(/*detail=*/(mask & 1u) != 0,
                                              /*heightBlend=*/(mask & 2u) != 0,
                                              /*distanceRescale=*/(mask & 4u) != 0,
                                              /*hexTiling=*/(mask & 8u) != 0);
            if (mask == 0)
                break;
        }
        code += "#endif\n";
        // Gated OUTSIDE the arm chain, so this is one extra #ifdef rather than 16 more arms.
        code += "#ifdef TERRAIN_MACRO_VARIATION\n";
        code += buildTerrainMacroVariationSnippet();
        code += "#endif\n";
        return code;
    }
}
