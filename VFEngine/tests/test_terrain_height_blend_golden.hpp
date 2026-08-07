#pragma once

// GENERATED DATA - do not hand-edit.
//
// Verbatim output of the terrain composite emitter's LINEAR arms, captured from the generator
// as it stood immediately before VK-1609 (height-blended layer compositing). test_terrain_height_blend.cpp
// compares buildTerrainCompositeLoop(detail, /*heightBlend=*/false) against these with ==.
//
// This is the executable form of VK-1609's acceptance criterion "layers without a height map fall
// back to linear blend bit-identical to today's output": the fallback arm is the same token stream
// as the pre-VK-1609 shader, so it compiles to the same SPIR-V and cannot drift by accident.
//
// If you INTEND to change the linear composite, regenerate these strings in the same commit and say
// so in the message - a silent update defeats the entire purpose of the check.
//
// INTENTIONAL UPDATE (terrain used-channel mask): the loop now reads a per-tile used-weight-channel
// mask from lodGeometricErrors2.w and skips channels with no weight anywhere on the tile BEFORE the
// sampleTileWeight fetch (32 -> 4*N SSBO loads/fragment). 0 means "no mask" and falls back to
// looping all 8 channels, so pre-mask tile data composites bit-identically to before.

namespace terrain_composite_golden
{
    constexpr const char* DETAIL_LINEAR = R"GLSL(vec3 ls_Albedo = vec3(0.0);
vec3 ls_Normal = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_EmissionScalar = 0.0;
vec3 ls_EmissionColor = vec3(0.0);
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
uint usedChMask = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.w);
if (usedChMask == 0u) { usedChMask = 0xFFu; }
for (int ch = 0; ch < 8; ch++) {
    if ((usedChMask & (1u << uint(ch))) == 0u) continue;
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic;
    if (ormIdx > 0u) {
        vec3 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
    }
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;
    if (emissionIdx > 0u) {
        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        ls_EmissionColor += emissionSample * layerEmission * w;
    } else {
        ls_EmissionScalar += layerEmission * w;
    }
    ls_Albedo += layerAlbedo * w;
    ls_Normal += layerNormal * w;
    ls_Roughness += layerRoughness * w;
    ls_Metallic += layerMetallic * w;
    ls_AO += layerAO * w;
    ls_Emission += layerEmission * w;
    ls_TotalW += w;
}
float ls_InvW = 1.0 / max(ls_TotalW, 0.001);
ls_Albedo *= ls_InvW;
ls_Normal *= ls_InvW;
ls_Roughness *= ls_InvW;
ls_Metallic *= ls_InvW;
ls_AO *= ls_InvW;
ls_Emission *= ls_InvW;
ls_EmissionScalar *= ls_InvW;
ls_EmissionColor *= ls_InvW;
// Terrain material properties
vec3 mat_albedo = ls_Albedo;
#define MAT_NORMALTS_DEFINED
float ls_NormalLengthSq = dot(ls_Normal, ls_Normal);
vec3 mat_normalTS = (ls_NormalLengthSq > 1e-8) ? ls_Normal * inversesqrt(ls_NormalLengthSq) : vec3(0.0, 0.0, 1.0);
float mat_metallic = ls_Metallic;
float mat_roughness = ls_Roughness;
float mat_ao = ls_AO;
#define MAT_EMISSION_DEFINED
vec3 mat_emission = mat_albedo * ls_EmissionScalar + ls_EmissionColor;
)GLSL";

    constexpr const char* PLAIN_LINEAR = R"GLSL(vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
uint usedChMask = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.w);
if (usedChMask == 0u) { usedChMask = 0xFFu; }
for (int ch = 0; ch < 8; ch++) {
    if ((usedChMask & (1u << uint(ch))) == 0u) continue;
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic;
    if (ormIdx > 0u) {
        vec3 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
    }
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    ls_Albedo += layerAlbedo * w;
    ls_Roughness += layerRoughness * w;
    ls_Metallic += layerMetallic * w;
    ls_AO += layerAO * w;
    ls_Emission += layerEmission * w;
    ls_TotalW += w;
}
float ls_InvW = 1.0 / max(ls_TotalW, 0.001);
ls_Albedo *= ls_InvW;
ls_Roughness *= ls_InvW;
ls_Metallic *= ls_InvW;
ls_AO *= ls_InvW;
ls_Emission *= ls_InvW;
// Terrain material properties
vec3 mat_albedo = ls_Albedo;
float mat_metallic = ls_Metallic;
float mat_roughness = ls_Roughness;
float mat_ao = ls_AO;
#define MAT_EMISSION_DEFINED
vec3 mat_emission = mat_albedo * ls_Emission;
)GLSL";
}
