#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D depthTexture;
layout(set = 1, binding = 1) uniform DoFParams {
    float focalDistance;
    float focalRange;
    float maxBlurRadius;
    float nearPlane;
    float farPlane;
    int sampleCount;
} dof;

// 16-sample Poisson disc
const vec2 poissonDisc[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

float linearizeDepth(float d)
{
    return dof.nearPlane * dof.farPlane / (dof.farPlane - d * (dof.farPlane - dof.nearPlane));
}

void main()
{
    float depth = texture(depthTexture, texCoord).r;
    float linearDepth = linearizeDepth(depth);

    // Circle of Confusion
    float coc = clamp(abs(linearDepth - dof.focalDistance) / dof.focalRange, 0.0, 1.0);

    vec2 texelSize = 1.0 / vec2(textureSize(sceneColorTexture, 0));
    float blurRadius = coc * dof.maxBlurRadius;

    vec3 color = vec3(0.0);
    float totalWeight = 0.0;

    int samples = min(dof.sampleCount, 16);

    for (int i = 0; i < samples; ++i)
    {
        vec2 offset = poissonDisc[i] * blurRadius * texelSize;
        color += texture(sceneColorTexture, texCoord + offset).rgb;
        totalWeight += 1.0;
    }

    color /= max(totalWeight, 1.0);

    outColor = vec4(color, coc);
}
