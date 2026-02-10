#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    float intensity;
} pc;

void main()
{
    // Radial direction from center
    vec2 dir = texCoord - vec2(0.5);
    float dist = length(dir);

    // Per-channel UV offset scaled by distance from center
    vec2 offset = dir * dist * pc.intensity;

    float r = texture(inputTexture, texCoord + offset).r;
    float g = texture(inputTexture, texCoord).g;
    float b = texture(inputTexture, texCoord - offset).b;

    outColor = vec4(r, g, b, 1.0);
}
