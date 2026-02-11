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
    float radius;
    float softness;
} pc;

void main()
{
    vec3 color = texture(inputTexture, texCoord).rgb;

    vec2 center = texCoord - 0.5;
    float dist = length(center);

    float vignette = smoothstep(pc.radius, pc.radius - pc.softness, dist);
    color *= mix(1.0, vignette, pc.intensity);

    outColor = vec4(color, 1.0);
}
