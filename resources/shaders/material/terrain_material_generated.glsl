// Generated terrain material shader code
// Generated terrain material code
// Per-tile palette: 8 channels with runtime indirection into palette of 2 layer(s)
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
    // VK-1209 finding #7: explicit-gradient samples (textureGrad) — the includer defines
    // triplanarWorldUVdx/dy in uniform control flow; implicit texture() here would take derivatives in
    // divergent flow (this continue / mesh_terrain's RVT branch) and shimmer at RVT seams.
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
