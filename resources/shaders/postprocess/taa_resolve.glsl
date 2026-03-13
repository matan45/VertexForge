#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 1, binding = 0) uniform sampler2D historyColor;
layout(set = 1, binding = 1) uniform sampler2D depthBuffer;
layout(set = 1, binding = 2) uniform TAAParams {
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec2 jitterOffset;
    vec2 texelSize;
    float blendFactor;
    uint frameIndex;
    uint useVarianceClipping;
    uint historyValid;
} params;

vec3 sampleCurrentColor(vec2 uv)
{
    return texture(currentColor, uv).rgb;
}

void main()
{
    vec3 current = sampleCurrentColor(texCoord);

    if (params.historyValid == 0u)
    {
        outColor = vec4(current, 1.0);
        return;
    }

    // Reconstruct world position from depth
    float depth = texture(depthBuffer, texCoord).r;
    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);
    vec4 worldPos = params.invViewProjection * clipPos;
    worldPos /= worldPos.w;

    // Reproject to previous frame
    vec4 prevClip = params.prevViewProjection * worldPos;
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Check if reprojected UV is valid
    bool validReproject = all(greaterThanEqual(prevUV, vec2(0.0))) &&
                          all(lessThanEqual(prevUV, vec2(1.0)));

    if (!validReproject)
    {
        outColor = vec4(current, 1.0);
        return;
    }

    vec3 history = texture(historyColor, prevUV).rgb;

    // Neighborhood clamping / variance clipping
    vec3 neighborMin = current;
    vec3 neighborMax = current;
    vec3 neighborSum = current;
    vec3 neighborSumSq = current * current;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(float(x), float(y)) * params.texelSize;
            vec3 s = sampleCurrentColor(texCoord + offset);
            neighborMin = min(neighborMin, s);
            neighborMax = max(neighborMax, s);
            neighborSum += s;
            neighborSumSq += s * s;
        }
    }

    vec3 clampedHistory;
    if (params.useVarianceClipping != 0u)
    {
        vec3 mean = neighborSum / 9.0;
        vec3 variance = neighborSumSq / 9.0 - mean * mean;
        vec3 stddev = sqrt(max(variance, vec3(0.0)));
        float gamma = 1.0;
        clampedHistory = clamp(history, mean - gamma * stddev, mean + gamma * stddev);
    }
    else
    {
        clampedHistory = clamp(history, neighborMin, neighborMax);
    }

    // Blend current with clamped history
    vec3 result = mix(clampedHistory, current, params.blendFactor);
    outColor = vec4(result, 1.0);
}
