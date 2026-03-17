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
    float planetRadius;
    float atmosphereRadius;
    float pad0[2];

    vec4 rayleighScattering;
    float mieScattering;
    float mieAbsorption;
    float mieAnisotropy;
    float mieDensityExpScale;

    vec4 ozoneAbsorption;
    float ozoneWidth;
    float pad1[3];

    vec4 sunIrradiance;
    vec4 sunDirection;
    vec4 groundAlbedo;
    vec4 cameraPosition;

    mat4 invViewProjection;
    mat4 viewProjection;

    float nearPlane;
    float farPlane;
    float aerialMaxDist;
    float aerialIntensity;

    uint screenWidth;
    uint screenHeight;
    float pad2[2];
} params;

void main()
{
    // DEBUG: pure gradient - no LUT, no atmosphere math
    // Reconstruct view direction from screen UV via inverse VP
    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 clipFar  = params.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec4 clipNear = params.invViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir = normalize(clipFar.xyz / clipFar.w - clipNear.xyz / clipNear.w);

    vec3 up = vec3(0.0, 1.0, 0.0);
    float cosZ = dot(viewDir, up);

    // Blue at top, orange at horizon, dark below
    float t = cosZ * 0.5 + 0.5; // remap [-1,1] to [0,1]
    vec3 zenith  = vec3(0.15, 0.3, 0.8);
    vec3 horizon = vec3(0.8, 0.55, 0.3);
    vec3 ground  = vec3(0.1, 0.08, 0.06);

    vec3 color;
    if (cosZ > 0.0)
        color = mix(horizon, zenith, cosZ);
    else
        color = mix(horizon, ground, -cosZ);

    outColor = vec4(color, 1.0);
}
