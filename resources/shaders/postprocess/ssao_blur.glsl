#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out float outBlurred;

layout(set = 0, binding = 0) uniform sampler2D ssaoTexture;
layout(set = 0, binding = 1) uniform sampler2D depthTexture;

layout(push_constant) uniform BlurParams {
    float nearPlane;
    float farPlane;
} blur;

float linearizeDepth(float d)
{
    return blur.nearPlane * blur.farPlane / (blur.farPlane - d * (blur.farPlane - blur.nearPlane));
}

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(ssaoTexture, 0));
    float centerDepth = linearizeDepth(texture(depthTexture, texCoord).r);

    float result = 0.0;
    float totalWeight = 0.0;

    // 4x4 bilateral blur
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUV = texCoord + offset;

            float sampleAO = texture(ssaoTexture, sampleUV).r;
            float sampleDepth = linearizeDepth(texture(depthTexture, sampleUV).r);

            // Bilateral weight: spatial * depth similarity
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * depthDiff * 100.0);

            float spatialWeight = 1.0 / (1.0 + float(x * x + y * y));

            float weight = spatialWeight * depthWeight;
            result += sampleAO * weight;
            totalWeight += weight;
        }
    }

    outBlurred = result / max(totalWeight, 0.001);
}
