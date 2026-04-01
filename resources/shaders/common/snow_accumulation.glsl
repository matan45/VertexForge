#ifndef SNOW_ACCUMULATION_GLSL
#define SNOW_ACCUMULATION_GLSL

// Snow material constants
const vec3 SNOW_ALBEDO = vec3(0.95, 0.95, 0.98);
const float SNOW_ROUGHNESS = 0.8;
const float SNOW_METALLIC = 0.0;
const float SNOW_NORMAL_BLEND = 0.7;
const float SNOW_ANGLE_THRESHOLD = 0.5; // cos(60 degrees) - steeper surfaces shed snow

void applySnowAccumulation(float snowAmount, vec3 worldNormal,
                           inout vec3 albedo, inout float roughness,
                           inout float metallic, inout vec3 N)
{
    if (snowAmount < 0.001) return;

    // Snow only accumulates on upward-facing surfaces
    float upDot = dot(normalize(worldNormal), vec3(0.0, 1.0, 0.0));
    float snowMask = smoothstep(SNOW_ANGLE_THRESHOLD, SNOW_ANGLE_THRESHOLD + 0.2, upDot);

    float snowFactor = snowAmount * snowMask;
    if (snowFactor < 0.001) return;

    // Blend material properties toward snow
    albedo = mix(albedo, SNOW_ALBEDO, snowFactor);
    roughness = mix(roughness, SNOW_ROUGHNESS, snowFactor);
    metallic = mix(metallic, SNOW_METALLIC, snowFactor);

    // Flatten normal toward up (snow smooths out surface detail)
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), snowFactor * SNOW_NORMAL_BLEND));
}

#endif // SNOW_ACCUMULATION_GLSL
