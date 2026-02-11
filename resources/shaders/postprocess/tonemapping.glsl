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
    float contrast;
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

// Gran Turismo tone mapping (Hajime Uchimura)
vec3 GranTurismo(vec3 x)
{
    float P = 1.0;   // max brightness
    float a = 1.0;   // contrast
    float m = 0.22;  // linear section start
    float l = 0.4;   // linear section length
    float c = 1.33;  // black tightness curve
    float b = 0.0;   // black tightness offset

    vec3 result;
    for (int i = 0; i < 3; ++i)
    {
        float v = x[i];
        float l0 = ((P - m) * l) / a;
        float S0 = m + l0;
        float S1 = m + a * l0;
        float C2 = (a * P) / (P - S1);
        float CP = -C2 / P;

        float w0 = 1.0 - smoothstep(0.0, m, v);
        float w2 = step(m + l0, v);
        float w1 = 1.0 - w0 - w2;

        float T = m * pow(v / m, c) + b;
        float L = m + a * (v - m);
        float S = P - (P - S1) * exp(CP * (v - S0));

        result[i] = T * w0 + L * w1 + S * w2;
    }
    return result;
}

// AgX tone mapping (Troy Sobotka / Blender)
vec3 agxDefaultContrastApprox(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

vec3 AgX(vec3 color)
{
    // AgX log2 encoding
    const mat3 agxTransform = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );

    const mat3 agxTransformInv = mat3(
        1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
       -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
       -0.0990297440797205, -0.0989611768448433,  1.15107367264116
    );

    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    color = agxTransform * color;
    color = clamp(log2(color), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = agxDefaultContrastApprox(color);
    color = agxTransformInv * color;

    return color;
}

// Khronos PBR Neutral tone mapping
vec3 KhronosPBRNeutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

void main()
{
    vec3 color = texture(inputTexture, texCoord).rgb;

    color *= pc.exposure;

    // Contrast around 0.18 midpoint
    color = max(pow(color / 0.18, vec3(pc.contrast)) * 0.18, 0.0);

    if (pc.mode == 0u)
    {
        color = ACESFilm(color);
    }
    else if (pc.mode == 1u)
    {
        color = Reinhard(color);
    }
    else if (pc.mode == 2u)
    {
        float W = 11.2;
        color = Uncharted2Tonemap(color) / Uncharted2Tonemap(vec3(W));
    }
    else if (pc.mode == 4u)
    {
        color = GranTurismo(color);
    }
    else if (pc.mode == 5u)
    {
        color = AgX(color);
    }
    else if (pc.mode == 6u)
    {
        color = KhronosPBRNeutral(color);
    }
    // mode == 3: Linear (exposure only, no curve)
    color = pow(color, vec3(1.0 / pc.gamma));

    outColor = vec4(color, 1.0);
}
