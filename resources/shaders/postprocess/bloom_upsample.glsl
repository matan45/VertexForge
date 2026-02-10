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
    float radius;
} pc;

void main()
{
    // 9-tap tent filter for smooth upsampling
    // Samples the smaller (lower) mip and spreads it over the larger target
    vec2 texelSize = pc.radius / vec2(textureSize(inputTexture, 0));

    vec3 result = vec3(0.0);

    // Center (weight 4)
    result += texture(inputTexture, texCoord).rgb * 4.0;

    // Edges (weight 2 each)
    result += texture(inputTexture, texCoord + vec2(-texelSize.x,  0.0)).rgb * 2.0;
    result += texture(inputTexture, texCoord + vec2( texelSize.x,  0.0)).rgb * 2.0;
    result += texture(inputTexture, texCoord + vec2( 0.0, -texelSize.y)).rgb * 2.0;
    result += texture(inputTexture, texCoord + vec2( 0.0,  texelSize.y)).rgb * 2.0;

    // Corners (weight 1 each)
    result += texture(inputTexture, texCoord + vec2(-texelSize.x, -texelSize.y)).rgb;
    result += texture(inputTexture, texCoord + vec2( texelSize.x, -texelSize.y)).rgb;
    result += texture(inputTexture, texCoord + vec2(-texelSize.x,  texelSize.y)).rgb;
    result += texture(inputTexture, texCoord + vec2( texelSize.x,  texelSize.y)).rgb;

    // Total weight: 4 + 2*4 + 1*4 = 16
    result /= 16.0;

    outColor = vec4(result, 1.0);
}
