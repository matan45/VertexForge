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
    float size;
    float time;
} pc;

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float grain(vec2 pixelCoord, float t)
{
    return hash(floor(pixelCoord / pc.size) + t) * 2.0 - 1.0;
}

void main()
{
    vec3 color = texture(inputTexture, texCoord).rgb;

    // Compute luminance for weighting
    float luma = dot(color, vec3(0.299, 0.587, 0.114));

    // Generate animated grain noise in pixel space
    vec2 pixelCoord = gl_FragCoord.xy;
    float noise = grain(pixelCoord, pc.time);

    // Luminance-weighted: stronger grain in darker areas
    float weight = (1.0 - luma) * pc.intensity;
    color += noise * weight;

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
