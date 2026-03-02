// Generated terrain material shader code
// Generated terrain material code
// Terrain Layer Stack - blending 2 layer(s)
vec3 ls_Albedo = vec3(0.0);
vec3 ls_Normal = vec3(0.0);
float ls_Roughness = 0.0;
float ls_Metallic = 0.0;
float ls_AO = 0.0;
float ls_Emission = 0.0;
float ls_TotalW = 0.0;
{ // Layer 0 (Layer 0) - blend: Linear
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 0u, fragTexCoord);
    vec2 layerUV = fragWorldUV * terrainLayers[0].tilingScale;
    uint albedoIdx_0 = terrainLayers[0].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_0)], layerUV).rgb : vec3(0.5);
    uint normalIdx_0 = terrainLayers[0].normalTextureIndex;
    vec3 layerNormal = (normalIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_0)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    uint ormIdx_0 = terrainLayers[0].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic;
    if (ormIdx_0 > 0u) {
        vec3 ormSample = texture(bindlessTextures[nonuniformEXT(ormIdx_0)], layerUV).rgb;
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
    } else {
        layerAO = terrainLayers[0].ao;
        layerRoughness = terrainLayers[0].roughness;
        layerMetallic = terrainLayers[0].metallic;
    }
    float layerEmission = terrainLayers[0].emissionStrength;
    ls_Albedo += layerAlbedo * w;
    ls_Normal += layerNormal * w;
    ls_Roughness += layerRoughness * w;
    ls_Metallic += layerMetallic * w;
    ls_AO += layerAO * w;
    ls_Emission += layerEmission * w;
    ls_TotalW += w;
}
{ // Layer 1 (Layer 1) - blend: Linear
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 1u, fragTexCoord);
    vec2 layerUV = fragWorldUV * terrainLayers[1].tilingScale;
    uint albedoIdx_1 = terrainLayers[1].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_1)], layerUV).rgb : vec3(0.5);
    uint normalIdx_1 = terrainLayers[1].normalTextureIndex;
    vec3 layerNormal = (normalIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_1)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    uint ormIdx_1 = terrainLayers[1].ormTextureIndex;
    float layerAO, layerRoughness, layerMetallic;
    if (ormIdx_1 > 0u) {
        vec3 ormSample = texture(bindlessTextures[nonuniformEXT(ormIdx_1)], layerUV).rgb;
        layerAO = ormSample.r;
        layerRoughness = ormSample.g;
        layerMetallic = ormSample.b;
    } else {
        layerAO = terrainLayers[1].ao;
        layerRoughness = terrainLayers[1].roughness;
        layerMetallic = terrainLayers[1].metallic;
    }
    float layerEmission = terrainLayers[1].emissionStrength;
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
