#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D edgeCompositeTexture;

void main()
{
    // The edge detection pass already composited scene + edges.
    // Just pass through the composited result.
    outColor = texture(edgeCompositeTexture, texCoord);
}
