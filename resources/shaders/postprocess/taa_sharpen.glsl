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
    float texelSizeX;
    float texelSizeY;
    float sharpenStrength;
} pc;

void main()
{
    vec2 texelSize = vec2(pc.texelSizeX, pc.texelSizeY);

    vec3 center = texture(inputTexture, texCoord).rgb;
    vec3 north  = texture(inputTexture, texCoord + vec2(0.0, -texelSize.y)).rgb;
    vec3 south  = texture(inputTexture, texCoord + vec2(0.0,  texelSize.y)).rgb;
    vec3 east   = texture(inputTexture, texCoord + vec2( texelSize.x, 0.0)).rgb;
    vec3 west   = texture(inputTexture, texCoord + vec2(-texelSize.x, 0.0)).rgb;

    // Contrast adaptive sharpening
    vec3 avg = (north + south + east + west) * 0.25;
    float localContrast = length(center - avg);
    float adaptiveStrength = pc.sharpenStrength * (1.0 - clamp(localContrast * 4.0, 0.0, 1.0));

    vec3 sharpened = center + (center - avg) * adaptiveStrength;
    outColor = vec4(max(sharpened, vec3(0.0)), 1.0);
}
