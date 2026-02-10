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
    float exposure;
    float gamma;
    uint mode;
} pc;

// ACES filmic tone mapping (Narkowicz approximation)
vec3 ACESFilm(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard tone mapping
vec3 Reinhard(vec3 x)
{
    return x / (1.0 + x);
}

// Uncharted 2 filmic tone mapping (Hable)
vec3 Uncharted2Tonemap(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

void main()
{
    vec3 color = texture(inputTexture, texCoord).rgb;

    // Apply exposure
    color *= pc.exposure;

    // Tone mapping
    if (pc.mode == 0u)
    {
        // ACES
        color = ACESFilm(color);
    }
    else if (pc.mode == 1u)
    {
        // Reinhard
        color = Reinhard(color);
    }
    else if (pc.mode == 2u)
    {
        // Uncharted 2
        float W = 11.2;
        color = Uncharted2Tonemap(color) / Uncharted2Tonemap(vec3(W));
    }
    // mode == 3: Linear (exposure only, no curve)

    // Gamma correction
    color = pow(color, vec3(1.0 / pc.gamma));

    outColor = vec4(color, 1.0);
}
