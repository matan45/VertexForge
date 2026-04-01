#ifndef WETNESS_GLSL
#define WETNESS_GLSL

void applyWetness(float wetness, inout vec3 albedo, inout float roughness,
                  inout float metallic, inout vec3 N)
{
    if (wetness < 0.001) return;

    // Porosity-based darkening: rough surfaces absorb more water and darken more
    float porosity = clamp(roughness * roughness, 0.0, 1.0);
    albedo *= mix(1.0, 0.6, wetness * porosity);

    // Wet surfaces are shinier
    roughness = mix(roughness, roughness * 0.3, wetness);

    // Slight Fresnel boost (water layer on surface)
    metallic = mix(metallic, max(metallic, 0.02), wetness);

    // Dampen normal detail at high wetness (water fills micro-crevices)
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), wetness * 0.3));
}

#endif // WETNESS_GLSL
