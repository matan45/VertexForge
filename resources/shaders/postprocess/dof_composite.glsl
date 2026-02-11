#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D dofBlurTexture;

void main()
{
    vec3 sceneColor = texture(sceneColorTexture, texCoord).rgb;
    vec4 dofResult = texture(dofBlurTexture, texCoord);

    // Mix original scene with blurred based on CoC stored in alpha
    vec3 finalColor = mix(sceneColor, dofResult.rgb, dofResult.a);

    outColor = vec4(finalColor, 1.0);
}
