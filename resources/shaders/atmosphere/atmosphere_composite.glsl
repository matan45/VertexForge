#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform sampler3D aerialPerspectiveLUT;

layout(set = 0, binding = 2) uniform AtmosphereCompositeParams {
    float nearPlane;
    float farPlane;
    float aerialMaxDist;
    float intensity;
} params;

float linearizeDepth(float windowZ, float near, float far)
{
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

void main()
{
    float rawDepth = texture(depthTexture, texCoord).r;

    // Skip sky pixels (depth = 1.0 in reverse-Z, or 0.0 in forward-Z)
    // Forward-Z: sky at 1.0
    if (rawDepth >= 0.9999)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0); // no scattering, full transmittance
        return;
    }

    float linearZ = linearizeDepth(rawDepth, params.nearPlane, params.farPlane);

    // Map linear depth to LUT Z coordinate (square root for better distribution)
    float depthFrac = clamp(sqrt(linearZ / params.aerialMaxDist), 0.0, 1.0);

    vec4 aerial = texture(aerialPerspectiveLUT, vec3(texCoord, depthFrac));

    outColor = vec4(aerial.rgb * params.intensity, aerial.a);
}
