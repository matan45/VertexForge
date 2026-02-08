// Generated terrain material shader code
// Generated terrain material code
// Terrain Layer Stack - blending 3 layer(s)
vec3 ls_Albedo = vec3(0.0);
vec3 ls_Normal = vec3(0.0);
float ls_TotalW = 0.0;
{ // Layer 0 (Layer 0) - blend: Linear
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 0u, fragTexCoord);
    vec2 layerUV = fragWorldUV * terrainLayers[0].tilingScale;
    uint albedoIdx_0 = terrainLayers[0].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_0)], layerUV).rgb : vec3(0.5);
    uint normalIdx_0 = terrainLayers[0].normalTextureIndex;
    vec3 layerNormal = (normalIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_0)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    ls_Albedo += layerAlbedo * w;
    ls_Normal += layerNormal * w;
    ls_TotalW += w;
}
{ // Layer 1 (Layer 1) - blend: Linear
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 1u, fragTexCoord);
    vec2 layerUV = fragWorldUV * terrainLayers[1].tilingScale;
    uint albedoIdx_1 = terrainLayers[1].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_1)], layerUV).rgb : vec3(0.5);
    uint normalIdx_1 = terrainLayers[1].normalTextureIndex;
    vec3 layerNormal = (normalIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_1)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    ls_Albedo += layerAlbedo * w;
    ls_Normal += layerNormal * w;
    ls_TotalW += w;
}
{ // Layer 2 (Layer 2) - blend: Linear
    float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 2u, fragTexCoord);
    vec2 layerUV = fragWorldUV * terrainLayers[2].tilingScale;
    uint albedoIdx_2 = terrainLayers[2].albedoTextureIndex;
    vec3 layerAlbedo = (albedoIdx_2 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_2)], layerUV).rgb : vec3(0.5);
    uint normalIdx_2 = terrainLayers[2].normalTextureIndex;
    vec3 layerNormal = (normalIdx_2 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_2)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
    ls_Albedo += layerAlbedo * w;
    ls_Normal += layerNormal * w;
    ls_TotalW += w;
}
float ls_InvW = 1.0 / max(ls_TotalW, 0.001);
ls_Albedo *= ls_InvW;
ls_Normal = normalize(ls_Normal);
// Terrain material properties
vec3 mat_albedo = ls_Albedo;
vec3 mat_normalTS = ls_Normal;
float mat_metallic = 0.0;
float mat_roughness = 0.9;
float mat_ao = 1.0;
