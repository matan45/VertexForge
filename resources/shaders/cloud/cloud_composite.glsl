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
    // Edge-aware upscale from half-res cloud result
    // Sample the 4 nearest half-res texels and pick nearest if there's a large
    // transmittance difference (cloud edge), otherwise use bilinear
    vec2 cloudTexSize = vec2(textureSize(cloudTexture, 0));
    vec2 halfTexelPos = texCoord * cloudTexSize - 0.5;
    ivec2 baseTexel = ivec2(floor(halfTexelPos));
    vec2 frac = halfTexelPos - vec2(baseTexel);

    vec4 s00 = texelFetch(cloudTexture, baseTexel, 0);
    vec4 s10 = texelFetch(cloudTexture, baseTexel + ivec2(1, 0), 0);
    vec4 s01 = texelFetch(cloudTexture, baseTexel + ivec2(0, 1), 0);
    vec4 s11 = texelFetch(cloudTexture, baseTexel + ivec2(1, 1), 0);

    // Detect edge: large transmittance spread means cloud boundary
    float tMin = min(min(s00.a, s10.a), min(s01.a, s11.a));
    float tMax = max(max(s00.a, s10.a), max(s01.a, s11.a));
    float edgeStrength = tMax - tMin;

    vec4 cloud;
    if (edgeStrength > 0.1)
    {
        // At cloud edge: use nearest to avoid halo
        ivec2 nearestTexel = baseTexel + ivec2(step(0.5, frac));
        cloud = texelFetch(cloudTexture, nearestTexel, 0);
    }
    else
    {
        // Interior/exterior: bilinear is fine
        cloud = texture(cloudTexture, texCoord);
    }

    vec3 cloudScattering = cloud.rgb;
    float cloudTransmittance = cloud.a;
    // Depth masking: only occlude clouds if real geometry is closer than cloud layer
    float depth = texture(depthTexture, texCoord).r;
    if (depth < 1)
    {
        float linearDepth = linearizeDepth(depth, pc.nearPlane, pc.farPlane);
        if (linearDepth > pc.cloudMinAlt)
        {
            float cloudFade = smoothstep(pc.cloudMinAlt * 0.3, pc.cloudMinAlt, linearDepth);
            cloudScattering *= cloudFade;
            cloudTransmittance = mix(1.0, cloudTransmittance, cloudFade);
        }
    }

    // Suppress thin cloud fringe: very low opacity edges add white halo
    // because HDR scattering is high while transmittance is still near 1
    float opacity = 1.0 - cloudTransmittance;
    float edgeMask = smoothstep(0.0, 0.08, opacity);
    cloudScattering *= edgeMask;
    opacity *= edgeMask;

    // Blend: scene * transmittance + scattering (premultiplied alpha)
    outColor = vec4(cloudScattering, opacity);
}
