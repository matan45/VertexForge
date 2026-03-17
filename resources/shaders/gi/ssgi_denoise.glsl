#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D ssgiTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;
    float nearPlane;
    float farPlane;
};

float linearizeDepth(float d)
{
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

void main()
{
    float centerDepth = linearizeDepth(texture(depthTexture, texCoord).r);

    vec4 result = vec4(0.0);
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            vec2 sampleUV = texCoord + vec2(x, y) * texelSize;
            vec4 sampleColor = texture(ssgiTexture, sampleUV);
            float sampleDepth = linearizeDepth(texture(depthTexture, sampleUV).r);

            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * depthDiff * 100.0);
            float spatialWeight = 1.0 / (1.0 + float(x * x + y * y));

            float weight = spatialWeight * depthWeight;
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    outColor = result / max(totalWeight, 0.001);
}
