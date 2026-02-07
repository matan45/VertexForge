// Generated terrain material shader code
// Generated terrain material code
    // Terrain Layer Stack - blending 2 layer(s)
    vec3 node_1_Albedo = vec3(0.0);
    vec3 node_1_Normal = vec3(0.0);
    float node_1_TotalW = 0.0;
    { // Layer 0 (gdr) - blend: Linear
        float w = 1.0; // layer 0 weight
        vec3 layerAlbedo = vec3(0.400000, 0.350000, 0.300000);
        vec3 layerNormal = vec3(0.0, 0.0, 1.0);
        node_1_Albedo += layerAlbedo * w;
        node_1_Normal += layerNormal * w;
        node_1_TotalW += w;
}
    { // Layer 1 (Layer 1) - blend: Linear
        float w = 0.0; // layer 1 weight
        vec3 layerAlbedo = vec3(0.400000, 0.350000, 0.300000);
        vec3 layerNormal = vec3(0.0, 0.0, 1.0);
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
