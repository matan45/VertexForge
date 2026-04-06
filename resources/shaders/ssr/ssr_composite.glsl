#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D ssrTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform PushConstants {
    float intensity;
    uint halfResolution;
    float nearPlane;
    float farPlane;
    vec2 texelSize;
};

float linearizeDepth(float d)
{
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

// Bilateral upsample from half-res SSR using full-res depth
vec3 bilateralUpsample(vec2 uv)
{
    float centerDepth = linearizeDepth(texture(depthTexture, uv).r);

    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 sampleUV = uv + vec2(x, y) * texelSize * 2.0;
            vec4 sampleColor = texture(ssrTexture, sampleUV);
            float sampleDepth = linearizeDepth(texture(depthTexture, sampleUV).r);

            float depthDiff = abs(centerDepth - sampleDepth);
            float weight = exp(-depthDiff * depthDiff * 50.0);

            result += sampleColor.rgb * sampleColor.a * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0 ? result / totalWeight : vec3(0.0);
}

void main()
{
    vec3 ssr;
    if (halfResolution != 0u)
    {
        ssr = bilateralUpsample(texCoord);
    }
    else
    {
        vec4 ssrSample = texture(ssrTexture, texCoord);
        ssr = ssrSample.rgb * ssrSample.a;
    }

    // Output SSR contribution — hardware additive blending adds to scene color
    outColor = vec4(ssr * intensity, 0.0);
}
