#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D godRaysTexture;

void main()
{
    vec3 sceneColor = texture(sceneColorTexture, texCoord).rgb;
    vec3 godRays = texture(godRaysTexture, texCoord).rgb;

    outColor = vec4(sceneColor + godRays, 1.0);
}
