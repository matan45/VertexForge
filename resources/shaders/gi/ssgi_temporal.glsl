#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D ssgiCurrentTexture;
layout(set = 1, binding = 0) uniform sampler2D ssgiHistoryTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform SSGIParams {
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 prevViewProjection;
    vec4 params;           // radius, maxDistance, intensity, temporalBlend
    vec2 resolution;
    vec2 texelSize;
    float nearPlane;
    float farPlane;
    uint sampleCount;
    uint frameIndex;
    uint historyValid;
    uint halfResolution;
} ssgi;

void main()
{
    vec4 current = texture(ssgiCurrentTexture, texCoord);

    if (ssgi.historyValid == 0u)
    {
        outColor = current;
        return;
    }

    // Reproject to previous frame UV
    float depth = texture(depthTexture, texCoord).r;
    if (depth >= 1.0)
    {
        outColor = current;
        return;
    }

    vec2 ndc = texCoord * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth, 1.0);
    vec4 viewPos = ssgi.inverseProjection * clipPos;
    viewPos /= viewPos.w;
    vec4 worldPos = ssgi.inverseView * viewPos;

    vec4 prevClip = ssgi.prevViewProjection * worldPos;
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;

    // Reject if out of bounds
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0))))
    {
        outColor = current;
        return;
    }

    vec4 history = texture(ssgiHistoryTexture, prevUV);

    // Neighborhood clamp for anti-ghosting (3x3)
    vec4 neighborMin = current;
    vec4 neighborMax = current;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec4 s = texture(ssgiCurrentTexture, texCoord + vec2(x, y) * ssgi.texelSize);
            neighborMin = min(neighborMin, s);
            neighborMax = max(neighborMax, s);
        }
    }

    vec4 clampedHistory = clamp(history, neighborMin, neighborMax);
    float blend = ssgi.params.w; // temporalBlend
    outColor = mix(clampedHistory, current, blend);
}
