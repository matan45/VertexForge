// Generated terrain material shader code
// Generated terrain material code
// Per-tile palette: 8 channels with runtime indirection into palette of 2 layer(s)
#ifdef TERRAIN_DETAIL_MAPS
#ifdef TERRAIN_HEIGHT_BLEND
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
#else
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
#endif
#else
#ifdef TERRAIN_HEIGHT_BLEND
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
#endif
