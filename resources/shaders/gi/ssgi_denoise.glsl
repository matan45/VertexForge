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
    vec2 direction;  // (1,0) for horizontal, (0,1) for vertical
};

float linearizeDepth(float d)
{
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

// 5-tap Gaussian weights (sigma ~= 1.5)
const float weights[3] = float[](0.40, 0.24, 0.06);

void main()
{
    float centerDepth = linearizeDepth(texture(depthTexture, texCoord).r);
    vec4 centerColor = texture(ssgiTexture, texCoord);

    vec4 result = centerColor * weights[0];
    float totalWeight = weights[0];

    for (int i = 1; i <= 2; i++)
    {
        vec2 offset = direction * texelSize * float(i);

        // Positive direction
        vec2 uvPos = texCoord + offset;
        vec4 samplePos = texture(ssgiTexture, uvPos);
        float depthPos = linearizeDepth(texture(depthTexture, uvPos).r);
        float depthDiffPos = abs(centerDepth - depthPos) / max(centerDepth, 0.001);
        float wPos = weights[i] * exp(-depthDiffPos * depthDiffPos * 200.0);

        // Negative direction
        vec2 uvNeg = texCoord - offset;
        vec4 sampleNeg = texture(ssgiTexture, uvNeg);
        float depthNeg = linearizeDepth(texture(depthTexture, uvNeg).r);
        float depthDiffNeg = abs(centerDepth - depthNeg) / max(centerDepth, 0.001);
        float wNeg = weights[i] * exp(-depthDiffNeg * depthDiffNeg * 200.0);

        result += samplePos * wPos + sampleNeg * wNeg;
        totalWeight += wPos + wNeg;
    }

    outColor = result / max(totalWeight, 0.001);
}
