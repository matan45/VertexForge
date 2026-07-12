
    vec3 albedo_linear = mat_albedo;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Construct TBN matrix from screen-space derivatives (needed for normal mapping)
    vec3 pos_dx = dFdx(fragWorldPos);
    vec3 pos_dy = dFdy(fragWorldPos);
    vec2 uv_dx = dFdx(fragTexCoord);
    vec2 uv_dy = dFdy(fragTexCoord);

    vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
    vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
    T = normalize(T - N * dot(N, T));
    B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

#ifdef USE_PARALLAX
    // Parallax Occlusion Mapping - only compiled when displacement texture is connected
    float heightScale = (mat_displacement - 0.5) * 0.1;
    vec3 viewDirTangent = normalize(transpose(TBN) * V);
    vec2 parallaxUV = parallaxOcclusionMapping(fragTexCoord, viewDirTangent, abs(heightScale));

    vec4 parallaxAlbedo = texture(u_Textures[TEX_SLOT_ALBEDO], parallaxUV);
    if (parallaxAlbedo.a > 0.01) {
        albedo_linear = parallaxAlbedo.rgb;
    }

    vec4 parallaxNormal = texture(u_Textures[TEX_SLOT_NORMAL], parallaxUV);
    if (length(parallaxNormal.rgb) > 0.01) {
        vec3 tangentNormal = parallaxNormal.rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }
#else
    // Normal mapping without parallax (default path - no branching overhead)
    vec3 tangentNormal = mat_normalTS * 2.0 - 1.0;
    if (abs(tangentNormal.x) > 0.001 || abs(tangentNormal.y) > 0.001 || tangentNormal.z < 0.999) {
        N = normalize(TBN * tangentNormal);
    }
#endif

    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo_linear, mat_metallic);

    float NdotV = max(dot(N, V), 0.0);
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 prefilteredColor = textureLod(prefilterMap, R, mat_roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, mat_roughness)).rg;

    vec3 specularScale;
    vec3 kD;
    multiScatterCompensation(F0, brdf, mat_metallic, specularScale, kD);

    vec3 diffuse = irradiance * albedo_linear * mat_iblDiffuse;
    vec3 specular = prefilteredColor * specularScale * mat_iblSpecular;

    float so = specularOcclusion(NdotV, mat_ao, mat_roughness);
    vec3 ambient = kD * diffuse * mat_ao + specular * so;

    vec3 emission_linear = mat_emissionColor * mat_emissionStrength;

#ifdef TOON_ENABLED
    // VK-1493 toon preview under a fixed synthetic key light (this footer has no scene
    // lights). Same band/spec/rim math as resources/shaders/common/toon_shading.glsl,
    // inlined because the material shader path does not resolve #include. Profile values
    // arrive as TOON_* defines from ShaderGraphCompiler.
    vec3 color;
    {
        const vec3 keyDir = normalize(vec3(0.4, 0.85, 0.5));
        float halfLambert = dot(N, keyDir) * 0.5 + 0.5;
        float t = clamp(halfLambert, 0.0, 1.0);                 // no shadow/atten in preview
        float bs = max(TOON_BAND_SMOOTHNESS, 1e-4);
        float midFactor = smoothstep(TOON_SHADOW_THRESHOLD - bs, TOON_SHADOW_THRESHOLD + bs, t);
        float litFactor = smoothstep(TOON_MID_THRESHOLD - bs, TOON_MID_THRESHOLD + bs, t);
        vec3 band = mix(TOON_SHADE_COLOR, TOON_MID_COLOR, midFactor);
        band = mix(band, albedo_linear, litFactor);            // lit band = albedo * white key

        vec3 H = normalize(V + keyDir);
        float ndh = clamp(dot(N, H), 0.0, 1.0);
        float spec = pow(ndh, max(TOON_SPEC_SHININESS, 1.0));
        float ss = max(TOON_SPEC_SMOOTHNESS, 1e-4);
        float blob = smoothstep(TOON_SPEC_THRESHOLD - ss, TOON_SPEC_THRESHOLD + ss, spec);
        vec3 toonSpec = blob * TOON_SPEC_INTENSITY * TOON_SPEC_COLOR;

        float rim = pow(1.0 - clamp(NdotV, 0.0, 1.0), max(TOON_RIM_POWER, 1e-3));
        vec3 toonRim = rim * TOON_RIM_INTENSITY * TOON_RIM_COLOR;

        vec3 toonAmbient = irradiance * albedo_linear * TOON_GI_SCALE;
        color = band + toonSpec + toonRim + toonAmbient + emission_linear;
    }
#else
    vec3 color = ambient + emission_linear;
#endif

    color = color / (color + vec3(1.0));

    color = pow(color, vec3(1.0/2.2));

    // LOD crossfade dithering (crossfade alpha is packed into lodLevel bits 8-15)
#ifdef CROSSFADE_ENABLED
    {
        float crossfadeAlpha = extractCrossfadeAlpha(drawData.lodLevel);
        if (crossfadeAlpha > 0.0 && ditherTest(gl_FragCoord.xy, crossfadeAlpha)) {
            discard;
        }
    }
#endif

    outColor = vec4(color, mat_opacity);
}
