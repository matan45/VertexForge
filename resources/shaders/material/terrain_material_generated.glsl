// Generated terrain material shader code
// Generated terrain material code
// Per-tile palette: 8 channels with runtime indirection into palette of 2 layer(s)
#if defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    HexTerrainBlend hbFar = hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hbFar) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb) : vec3(0.5), lsFarT);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleNormal(normalIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;
    if (emissionIdx > 0u) {
        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        ls_EmissionColor += emissionSample * layerEmission * bw;
    } else {
        ls_EmissionScalar += layerEmission * bw;
    }
    ls_Albedo += layerAlbedo * bw;
    ls_Normal += layerNormal * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif !defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    HexTerrainBlend hbFar = hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hbFar) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb) : vec3(0.5), lsFarT);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    ls_Albedo += layerAlbedo * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    HexTerrainBlend hbFar = hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hbFar) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb) : vec3(0.5), lsFarT);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleNormal(normalIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
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
#elif !defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    HexTerrainBlend hbFar = hexTerrainComputeBlend(layerFarUV, layerFarUVdx, layerFarUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hbFar) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb) : vec3(0.5), lsFarT);
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
#elif defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleNormal(normalIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;
    if (emissionIdx > 0u) {
        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        ls_EmissionColor += emissionSample * layerEmission * bw;
    } else {
        ls_EmissionScalar += layerEmission * bw;
    }
    ls_Albedo += layerAlbedo * bw;
    ls_Normal += layerNormal * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif !defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    ls_Albedo += layerAlbedo * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleNormal(normalIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0) : vec3(0.0, 0.0, 1.0);
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
#elif !defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float hexStrength = terrainLayers[paletteIdx].hexTilingStrength;
    HexTerrainBlend hb = hexTerrainComputeBlend(layerUV, layerUVdx, layerUVdy, terrainLayers[paletteIdx].hexCellScale, terrainLayers[paletteIdx].hexContrast, terrainLayers[paletteIdx].hexRotationStrength);
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? ((hexStrength > 0.0) ? hexTerrainSampleAlbedo(albedoIdx, hb) : textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb) : vec3(0.5);
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
#elif defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb : vec3(0.5), lsFarT);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], layerUV, layerUVdx, layerUVdy).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;
    if (emissionIdx > 0u) {
        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        ls_EmissionColor += emissionSample * layerEmission * bw;
    } else {
        ls_EmissionScalar += layerEmission * bw;
    }
    ls_Albedo += layerAlbedo * bw;
    ls_Normal += layerNormal * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif !defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb : vec3(0.5), lsFarT);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    ls_Albedo += layerAlbedo * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    // Explicit gradients remain valid inside the divergent layer loop and RVT fallback branch.
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb : vec3(0.5), lsFarT);
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
#elif !defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
    uint packedWord = (ch < 4) ? packedLI : packedLI2;
    uint paletteIdx = (packedWord >> ((ch % 4) * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = triplanarWorldUV * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdx = triplanarWorldUVdx * terrainLayers[paletteIdx].tilingScale;
    vec2 layerUVdy = triplanarWorldUVdy * terrainLayers[paletteIdx].tilingScale;
    float lsFpLog2 = terrainFootprintLog2 + log2(terrainLayers[paletteIdx].tilingScale);
    float lsFarT = smoothstep(terrainAntiTiling.rescaleKneeLog2, terrainAntiTiling.rescaleKneeLog2 + terrainAntiTiling.rescaleWidthLog2, lsFpLog2) * terrainAntiTiling.rescaleStrength;
    vec2 layerFarUV = layerUV * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdx = layerUVdx * terrainAntiTiling.rescaleScale;
    vec2 layerFarUVdy = layerUVdy * terrainAntiTiling.rescaleScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV, layerUVdx, layerUVdy).rgb : vec3(0.5);
    layerAlbedo = mix(layerAlbedo, (albedoIdx > 0u) ? textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], layerFarUV, layerFarUVdx, layerFarUVdy).rgb : vec3(0.5), lsFarT);
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
#elif defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
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
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    uint emissionIdx = terrainLayers[paletteIdx].emissionTextureIndex;
    if (emissionIdx > 0u) {
        vec3 emissionSample = textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], layerUV, layerUVdx, layerUVdy).rgb;
        ls_EmissionColor += emissionSample * layerEmission * bw;
    } else {
        ls_EmissionScalar += layerEmission * bw;
    }
    ls_Albedo += layerAlbedo * bw;
    ls_Normal += layerNormal * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif !defined(TERRAIN_DETAIL_MAPS) && defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
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
    float layerAO, layerRoughness, layerMetallic, layerHeight;
    if (ormIdx > 0u) {
        vec4 ormSample = textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], layerUV, layerUVdx, layerUVdy);
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
        layerHeight = ormSample.a;
    } else {
        layerAO = terrainLayers[paletteIdx].ao;
        layerRoughness = terrainLayers[paletteIdx].roughness;
        layerMetallic = terrainLayers[paletteIdx].metallic;
        layerHeight = 0.5;
    }
    float hbContrast = terrainLayers[paletteIdx].heightBlendContrast;
    float hbAlpha = step(1e-6, hbContrast) * smoothstep(0.001, 0.02, w);
    float bw = w * mix(1.0, exp2(hbContrast * (layerHeight - 0.5)), hbAlpha);
    float layerEmission = terrainLayers[paletteIdx].emissionStrength;
    ls_Albedo += layerAlbedo * bw;
    ls_Roughness += layerRoughness * bw;
    ls_Metallic += layerMetallic * bw;
    ls_AO += layerAO * bw;
    ls_Emission += layerEmission * bw;
    ls_TotalW += bw;
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
#elif defined(TERRAIN_DETAIL_MAPS) && !defined(TERRAIN_HEIGHT_BLEND) && !defined(TERRAIN_DISTANCE_RESCALE) && !defined(TERRAIN_HEX_TILING)
vec3 ls_Albedo = vec3(0.0);
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
for (int ch = 0; ch < 8; ch++) {
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
#else
vec3 ls_Albedo = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
uint packedLI2 = floatBitsToUint(tiles[fragTileIndex].lodGeometricErrors2.z);
for (int ch = 0; ch < 8; ch++) {
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
#endif
#ifdef TERRAIN_MACRO_VARIATION
// VK-1611 world-anchored macro variation (identity at strength 0).
float tmv_n0 = terrainValueNoise2D(terrainWorldXZ * terrainAntiTiling.macroFrequency0, terrainAntiTiling.macroSeed);
float tmv_n1 = terrainValueNoise2D(terrainWorldXZ * terrainAntiTiling.macroFrequency1, terrainAntiTiling.macroSeed ^ 0x9E3779B9u);
mat_albedo *= (1.0 + terrainAntiTiling.macroStrength * (tmv_n0 - 0.5)) * (1.0 + terrainAntiTiling.macroStrength * (tmv_n1 - 0.5));
#endif
