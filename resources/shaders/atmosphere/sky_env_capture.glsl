// VK-1569 — Dynamic sky -> IBL ambient: capture the atmosphere sky into an env cubemap face.
// Renders cube geometry per face (viewProj push constant); the fragment samples the atmosphere
// sky-view LUT for the face direction and adds the moon/night terms, DELIBERATELY EXCLUDING the
// sun disk (the sun is direct light) and the star field (negligible diffuse irradiance, and it
// would alias/flicker at 128^2 across the time-sliced capture).

#type VERTEX
#version 460 core

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 WorldPos;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main()
{
    WorldPos = position;
    gl_Position = pc.viewProj * vec4(position, 1.0);
}

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "atmosphere_common.glsl"

layout(location = 0) in vec3 WorldPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D skyViewLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;

layout(std140, set = 0, binding = 2) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

void main()
{
    vec3 viewDir = normalize(WorldPos);

    float altitude = params.cameraPosition.w;
    float pRadius = params.planetParams.x;
    float aRadius = params.planetParams.y;
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 sunDir = normalize(params.sunDirection.xyz);

    // Multiple-scattered sky color (the sky-view LUT already excludes the sun disk).
    vec2 skyUV = directionToSkyViewUV(viewDir, up, altitude, pRadius, aRadius);
    vec3 skyColor = texture(skyViewLUT, clamp(skyUV, vec2(0.001), vec2(0.999))).rgb;

    // Sun disk intentionally EXCLUDED here (matches UE5 Sky Light: the sun is direct light).

    // Moon glow + disk (ported from atmosphere_sky.glsl, simplified for low-frequency ambient).
    vec3 moonDir = normalize(params.moonDirection.xyz);
    float moonAngRad = params.moonDirection.w;
    float moonBrightness = params.moonParams.x;
    float moonPhase = params.moonParams.y;
    float cosMoonAngle = dot(viewDir, moonDir);

    if (cosMoonAngle > cos(moonAngRad * 6.0) && moonBrightness > 0.0)
    {
        float glowAngle = acos(clamp(cosMoonAngle, -1.0, 1.0));
        float glowFalloff = exp(-glowAngle * glowAngle / (moonAngRad * moonAngRad * 3.0));
        float cosMoonZenith = dot(up, moonDir);
        vec3 transToMoon = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosMoonZenith);
        if (dot(transToMoon, vec3(0.333)) < 0.0001)
            transToMoon = vec3(0.0);
        vec3 glowColor = vec3(0.7, 0.8, 1.0) * moonBrightness * 0.15;
        skyColor += glowColor * glowFalloff * transToMoon * moonPhase;
    }

    if (cosMoonAngle > cos(moonAngRad) && moonBrightness > 0.0)
    {
        float moonEdge = smoothstep(cos(moonAngRad * 1.1), cos(moonAngRad * 0.9), cosMoonAngle);
        float cosMoonZenith = dot(up, moonDir);
        vec3 transToMoon = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosMoonZenith);
        vec3 moonColor = vec3(0.95, 0.93, 0.88);
        skyColor += moonColor * params.sunIrradiance.xyz * moonBrightness * transToMoon * moonEdge * moonPhase;
    }

    // Night-sky ambient floor (fades in as the sun drops below the horizon).
    float sunElev = dot(up, sunDir);
    float nightFactor = 1.0 - smoothstep(-0.15, 0.0, sunElev);
    skyColor += vec3(params.moonParams.z) * nightFactor;

    outColor = vec4(max(skyColor, vec3(0.0)), 1.0);
}
