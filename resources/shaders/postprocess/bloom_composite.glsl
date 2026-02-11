#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTexture;
layout(set = 1, binding = 0) uniform sampler2D bloomTexture;

layout(push_constant) uniform PushConstants {
    float intensity;
} pc;

void main()
{
    vec3 scene = texture(sceneTexture, texCoord).rgb;
    vec3 bloom = texture(bloomTexture, texCoord).rgb;

    outColor = vec4(scene + bloom * pc.intensity, 1.0);
}
