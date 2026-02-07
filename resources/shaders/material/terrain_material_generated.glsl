// Generated terrain material shader code
// Generated terrain material code
    // Terrain Layer Stack - blending 2 layer(s)
    vec3 node_1_Albedo = vec3(0.0);
    vec3 node_1_Normal = vec3(0.0);
    float node_1_TotalW = 0.0;
    { // Layer 0 (gold) - blend: Linear
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 0u, fragTexCoord);
        vec2 layerUV = fragWorldUV * 1.000000;
        // albedo: C:\matan\texture\gold\albedo.vfImage
        // normal: C:\matan\texture\gold\normal.vfImage
        vec3 layerAlbedo = vec3(0.20, 0.55, 0.20); // placeholder until GPU textures bound
        vec3 layerNormal = vec3(0.0, 0.0, 1.0); // placeholder
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    { // Layer 1 (grass) - blend: Linear
        float w = sampleTileWeight(tiles[fragTileIndex].weightMapOffset, uint(tiles[fragTileIndex].aabbMin.w), 1u, fragTexCoord);
        vec2 layerUV = fragWorldUV * 1.000000;
        // albedo: C:\matan\texture\grass\albedo.vfImage
        // normal: C:\matan\texture\grass\normal.vfImage
        vec3 layerAlbedo = vec3(0.55, 0.40, 0.20); // placeholder until GPU textures bound
        vec3 layerNormal = vec3(0.0, 0.0, 1.0); // placeholder
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
