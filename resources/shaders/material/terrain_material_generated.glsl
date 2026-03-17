// Generated terrain material shader code
// Generated terrain material code
// Per-tile palette: 4 channels with runtime indirection into palette of 1 layer(s)
vec3 ls_Albedo = vec3(0.0);
vec3 ls_Normal = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
uint packedLI = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
// Must match WEIGHT_CHANNELS (terrain/TerrainWeightMap.hpp) — 4 channels, 8 bits each
for (int ch = 0; ch < 4; ch++) {
    uint paletteIdx = (packedLI >> (ch * 8u)) & 0xFFu;
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), uint(ch), fragTexCoord);
    if (w < 0.001) continue;
    vec2 layerUV = fragWorldUV * terrainLayers[paletteIdx].tilingScale;
    uint albedoIdx = terrainLayers[paletteIdx].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx)], layerUV).rgb : vec3(0.5);
    uint normalIdx = terrainLayers[paletteIdx].normalTextureIndex;
    vec3 layerNormal = (normalIdx > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    uint ormIdx = terrainLayers[paletteIdx].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic;
    if (ormIdx > 0u) {
        vec3 ormSample = texture(bindlessTextures[nonuniformEXT(ormIdx)], layerUV).rgb;
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
    ls_Normal += layerNormal * w;
    ls_Roughness += layerRoughness * w;
    ls_Metallic += layerMetallic * w;
    ls_AO += layerAO * w;
    ls_Emission += layerEmission * w;
    ls_TotalW += w;
}
float ls_InvW = 1.0 / max(ls_TotalW, 0.001);
ls_Albedo *= ls_InvW;
ls_Normal = normalize(ls_Normal);
ls_Roughness *= ls_InvW;
ls_Metallic *= ls_InvW;
ls_AO *= ls_InvW;
ls_Emission *= ls_InvW;
// Terrain material properties
vec3 mat_albedo = ls_Albedo;
vec3 mat_normalTS = ls_Normal;
float mat_metallic = ls_Metallic;
float mat_roughness = ls_Roughness;
float mat_ao = ls_AO;
#define MAT_EMISSION_DEFINED
vec3 mat_emission = mat_albedo * ls_Emission;
