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
    // Reconstruct world-space view direction from screen UV
    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 worldPos = params.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec3 viewDir = normalize(worldPos.xyz / worldPos.w - params.cameraPosition.xyz);

    float altitude = params.cameraPosition.w;
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 sunDir = normalize(params.sunDirection.xyz);

    // Sample sky-view LUT
    vec2 skyUV = directionToSkyViewUV(viewDir, up, altitude, params.planetRadius, params.atmosphereRadius);
    vec3 skyColor = texture(skyViewLUT, clamp(skyUV, vec2(0.001), vec2(0.999))).rgb;

    // Sun disk
    float cosAngle = dot(viewDir, sunDir);
    float sunAngularRadius = params.sunIrradiance.w;
    if (cosAngle > cos(sunAngularRadius))
    {
        // Smooth edge
        float edge = smoothstep(cos(sunAngularRadius * 1.1), cos(sunAngularRadius * 0.9), cosAngle);
        float cosZenith = dot(up, sunDir);
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT,
            params.planetRadius, params.atmosphereRadius, altitude, cosZenith);
        skyColor += params.sunIrradiance.xyz * transToSun * edge;
    }

    outColor = vec4(skyColor, 1.0);
}
