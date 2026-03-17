#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D cloudTexture;    // half-res cloud result
layout(set = 0, binding = 1) uniform sampler2D depthTexture;    // full-res scene depth

layout(push_constant) uniform PushConstants {
    float nearPlane;
    float farPlane;
    float cloudMinAlt;
    float cloudMaxAlt;
} pc;

float linearizeDepth(float d, float near, float far)
{
    return near * far / (far - d * (far - near));
}

void main()
{
    // Sample cloud result (bilinear upscale from half-res)
    vec4 cloud = texture(cloudTexture, texCoord);
    vec3 cloudScattering = cloud.rgb;
    float cloudTransmittance = cloud.a;

    // Sample scene depth
    float depth = texture(depthTexture, texCoord).r;

    // If geometry is in front of cloud layer, skip cloud rendering
    if (depth < 1.0)
    {
        float linearDepth = linearizeDepth(depth, pc.nearPlane, pc.farPlane);

        // Fade clouds behind near geometry smoothly
        float cloudFade = smoothstep(0.0, pc.cloudMinAlt * 0.5, linearDepth);
        cloudScattering *= cloudFade;
        cloudTransmittance = mix(1.0, cloudTransmittance, cloudFade);
    }

    // Blend: scene * transmittance + scattering (premultiplied alpha)
    outColor = vec4(cloudScattering, 1.0 - cloudTransmittance);
}
