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

layout(push_constant) uniform EdgeParams {
    float threshold;
    float edgeWidth;
    float edgeColorR;
    float edgeColorG;
    float edgeColorB;
    float opacity;
    float nearPlane;
    float farPlane;
} edge;

float linearizeDepth(float d)
{
    return edge.nearPlane * edge.farPlane / (edge.farPlane - d * (edge.farPlane - edge.nearPlane));
}

void main()
{
    vec3 sceneColor = texture(sceneColorTexture, texCoord).rgb;
    vec2 texelSize = edge.edgeWidth / vec2(textureSize(depthTexture, 0));

    // Sample 3x3 neighborhood and linearize
    float d00 = linearizeDepth(texture(depthTexture, texCoord + vec2(-texelSize.x, -texelSize.y)).r);
    float d10 = linearizeDepth(texture(depthTexture, texCoord + vec2( 0.0,         -texelSize.y)).r);
    float d20 = linearizeDepth(texture(depthTexture, texCoord + vec2( texelSize.x, -texelSize.y)).r);
    float d01 = linearizeDepth(texture(depthTexture, texCoord + vec2(-texelSize.x,  0.0)).r);
    float d21 = linearizeDepth(texture(depthTexture, texCoord + vec2( texelSize.x,  0.0)).r);
    float d02 = linearizeDepth(texture(depthTexture, texCoord + vec2(-texelSize.x,  texelSize.y)).r);
    float d12 = linearizeDepth(texture(depthTexture, texCoord + vec2( 0.0,          texelSize.y)).r);
    float d22 = linearizeDepth(texture(depthTexture, texCoord + vec2( texelSize.x,  texelSize.y)).r);

    // Normalize by far plane for consistent thresholds
    float invFar = 1.0 / edge.farPlane;
    d00 *= invFar; d10 *= invFar; d20 *= invFar;
    d01 *= invFar; d21 *= invFar;
    d02 *= invFar; d12 *= invFar; d22 *= invFar;

    // Sobel horizontal: Gx
    float gx = -d00 + d20
             - 2.0 * d01 + 2.0 * d21
             - d02 + d22;

    // Sobel vertical: Gy
    float gy = -d00 - 2.0 * d10 - d20
             + d02 + 2.0 * d12 + d22;

    float edgeMagnitude = sqrt(gx * gx + gy * gy);
    float edgeMask = smoothstep(edge.threshold * 0.5, edge.threshold * 1.5, edgeMagnitude);

    vec3 edgeColor = vec3(edge.edgeColorR, edge.edgeColorG, edge.edgeColorB);
    vec3 finalColor = mix(sceneColor, edgeColor, edgeMask * edge.opacity);

    outColor = vec4(finalColor, 1.0);
}
