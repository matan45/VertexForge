
    // Convert albedo from sRGB to linear space for PBR calculations
    vec3 albedo_linear = pow(mat_albedo, vec3(2.2));

    // PBR Lighting
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Apply normal mapping if mat_normalTS differs from default (0,0,1)
    // Normal map values from texture are in [0,1], need to convert to [-1,1]
    vec3 tangentNormal = mat_normalTS * 2.0 - 1.0;

    // Only apply if it's not the default normal (pointing up in tangent space)
    if (abs(tangentNormal.x) > 0.001 || abs(tangentNormal.y) > 0.001 || tangentNormal.z < 0.999) {
        // Construct TBN matrix from screen-space derivatives
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        // Calculate tangent and bitangent
        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);

        // Ensure orthogonal TBN
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);

        // Transform normal from tangent space to world space
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * tangentNormal);
    }

    vec3 R = reflect(-V, N);

    // Calculate F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo_linear, mat_metallic);

    // IBL Ambient Lighting
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, mat_roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - mat_metallic;

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo_linear * mat_iblDiffuse;

    // Specular IBL
    vec3 prefilteredColor = textureLod(prefilterMap, R, mat_roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), mat_roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * mat_iblSpecular;

    // Combine
    vec3 ambient = (kD * diffuse + specular) * mat_ao;

    // Emission - convert from sRGB to linear space if from texture
    vec3 emission_linear = pow(mat_emissionColor, vec3(2.2)) * mat_emissionStrength;

    vec3 color = ambient + emission_linear;

    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, mat_opacity);
}
