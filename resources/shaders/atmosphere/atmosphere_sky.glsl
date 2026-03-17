#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "atmosphere_common.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D skyViewLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;

layout(std140, set = 0, binding = 2) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

void main()
{
    // Reconstruct view direction from screen UV via inverse VP
    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 clipFar  = params.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec4 clipNear = params.invViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir = normalize(clipFar.xyz / clipFar.w - clipNear.xyz / clipNear.w);

    float altitude = params.cameraPosition.w;
    float pRadius = params.planetParams.x;
    float aRadius = params.planetParams.y;
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 sunDir = normalize(params.sunDirection.xyz);

    // Sample sky-view LUT
    vec2 skyUV = directionToSkyViewUV(viewDir, up, altitude, pRadius, aRadius);
    vec3 skyColor = texture(skyViewLUT, clamp(skyUV, vec2(0.001), vec2(0.999))).rgb;

    // Below horizon: keep the LUT ground color (scene geometry renders on top)

    // Sun disk
    float cosAngle = dot(viewDir, sunDir);
    float sunAngRad = params.sunIrradiance.w;
    if (cosAngle > cos(sunAngRad))
    {
        float edge = smoothstep(cos(sunAngRad * 1.1), cos(sunAngRad * 0.9), cosAngle);
        float cosZenith = dot(up, sunDir);
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT, pRadius, aRadius, altitude, cosZenith);
        float transLum = dot(transToSun, vec3(0.333));
        if (transLum < 0.0001)
            transToSun = vec3(1.0);
        skyColor += params.sunIrradiance.xyz * transToSun * edge;
    }

    outColor = vec4(skyColor, 1.0);
}
