// Generated terrain material shader code
// Generated terrain material code
    // Terrain Layer Stack - blending 4 layer(s)
    vec3 node_1_Albedo = vec3(0.0);
    vec3 node_1_Normal = vec3(0.0);
    float node_1_TotalW = 0.0;
    { // Layer 0 (gold) - blend: Linear
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 0u, fragTexCoord);
        vec2 layerUV = fragWorldUV * terrainLayers[0].tilingScale;
        uint albedoIdx_0 = terrainLayers[0].albedoTextureIndex;
        vec3 layerAlbedo = (albedoIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_0)], layerUV).rgb : vec3(0.5);
        uint normalIdx_0 = terrainLayers[0].normalTextureIndex;
        vec3 layerNormal = (normalIdx_0 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_0)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    { // Layer 1 (gress) - blend: Linear
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 1u, fragTexCoord);
        vec2 layerUV = fragWorldUV * terrainLayers[1].tilingScale;
        uint albedoIdx_1 = terrainLayers[1].albedoTextureIndex;
        vec3 layerAlbedo = (albedoIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_1)], layerUV).rgb : vec3(0.5);
        uint normalIdx_1 = terrainLayers[1].normalTextureIndex;
        vec3 layerNormal = (normalIdx_1 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_1)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    { // Layer 2 (wall) - blend: HeightBased
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 2u, fragTexCoord);
        vec2 layerUV = fragWorldUV * terrainLayers[2].tilingScale;
        uint albedoIdx_2 = terrainLayers[2].albedoTextureIndex;
        vec3 layerAlbedo = (albedoIdx_2 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_2)], layerUV).rgb : vec3(0.5);
        uint normalIdx_2 = terrainLayers[2].normalTextureIndex;
        vec3 layerNormal = (normalIdx_2 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_2)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    { // Layer 3 (iron) - blend: Linear
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 3u, fragTexCoord);
        vec2 layerUV = fragWorldUV * terrainLayers[3].tilingScale;
        uint albedoIdx_3 = terrainLayers[3].albedoTextureIndex;
        vec3 layerAlbedo = (albedoIdx_3 > 0u) ? texture(bindlessTextures[nonuniformEXT(albedoIdx_3)], layerUV).rgb : vec3(0.5);
        uint normalIdx_3 = terrainLayers[3].normalTextureIndex;
        vec3 layerNormal = (normalIdx_3 > 0u) ? texture(bindlessTextures[nonuniformEXT(normalIdx_3)], layerUV).rgb * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    float node_1_InvW = 1.0 / max(node_1_TotalW, 0.001);
    node_1_Albedo *= node_1_InvW;
    node_1_Normal = normalize(node_1_Normal);
    // Terrain material properties from shader graph
    vec3 mat_albedo = node_1_Albedo;
    vec3 mat_normalTS = node_1_Normal;
    float mat_metallic = 0.0;
    float mat_roughness = 0.9;
    float mat_ao = 1.0;
