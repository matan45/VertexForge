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

namespace editor::graph
{
    // Emits the per-tile terrain composite loop for one shader permutation. All permutations
    // share a single body here so the per-layer logic (weight sampling, ORM unpack, tiling /
    // gradient math) can only be edited in one place.
    //   `detail`      inserts the extra work the TERRAIN_DETAIL_MAPS permutation samples — the
    //                 normal-map fetch, the emission-texture path, and the mat_normalTS output.
    //   `heightBlend` inserts the VK-1609 TERRAIN_HEIGHT_BLEND weight sharpening.
    // With heightBlend == false the output is byte-identical to the pre-VK-1609 emitter: the
    // accumulation operand is the literal `w`, the ORM sample stays a vec3, and not one extra
    // token is emitted. That is bit-identity by construction rather than by inspection.
    inline std::string buildTerrainCompositeLoop(bool detail, bool heightBlend)
    {
        // Naming the accumulation operand once is what keeps the two arms in lockstep. The two
        // emission accumulators below sit inside an `if (emissionIdx > 0u)` block, visually
        // separated from the main accumulation group; if they kept `* w` while ls_TotalW
        // accumulated the sharpened weight, terrain emission would be normalized by the wrong
        // denominator and silently drift with height.
        const std::string W = heightBlend ? "bw" : "w";

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
        s += "for (int ch = 0; ch < 8; ch++) {\n";
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
        s += "    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;\n";
        s += "    vec3 layerAlbedo = (albedoIdx > 0u) ? "
             "textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);\n";
        // The non-detail permutation lights terrain with the geometric normal only (mesh_terrain
        // uses N = normalize(fragNormal); the RVT bake writes no normal plane), so it omits the
        // per-layer normal fetch — a composited tangent-space normal would be dead work there.
        if (detail)
        {
            s += "    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;\n";
            s += "    vec3 layerNormal = (normalIdx > 0u) ? "
                 "textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);\n";
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

    // The four-arm preprocessor assembly written to terrain_material_generated.glsl, without the
    // leading provenance comments (those carry the material's layer count and so are emitted by
    // ShaderGraphCompiler::compileTerrainMaterial). Keeping the assembly here means the golden
    // test can assert the checked-in file ends with exactly this text.
    inline std::string buildTerrainCompositeSnippet()
    {
        std::string code;
        code += "#ifdef TERRAIN_DETAIL_MAPS\n";
        code += "#ifdef TERRAIN_HEIGHT_BLEND\n";
        code += buildTerrainCompositeLoop(/*detail=*/true, /*heightBlend=*/true);
        code += "#else\n";
        code += buildTerrainCompositeLoop(/*detail=*/true, /*heightBlend=*/false);
        code += "#endif\n";
        code += "#else\n";
        code += "#ifdef TERRAIN_HEIGHT_BLEND\n";
        code += buildTerrainCompositeLoop(/*detail=*/false, /*heightBlend=*/true);
        code += "#else\n";
        code += buildTerrainCompositeLoop(/*detail=*/false, /*heightBlend=*/false);
        code += "#endif\n";
        code += "#endif\n";
        return code;
    }
}
