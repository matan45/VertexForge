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
    float threshold;
    uint isFirstPass;
} pc;

// Karis average for anti-firefly weighting
float karisWeight(vec3 c)
{
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

// Soft threshold curve (avoids hard cutoff)
vec3 prefilter(vec3 c)
{
    float brightness = max(c.r, max(c.g, c.b));
    float soft = brightness - pc.threshold + 0.1;
    soft = clamp(soft, 0.0, 0.2);
    soft = soft * soft / (0.4 + 1e-4);
    float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-4);
    return c * max(contribution, 0.0);
}

void main()
{
    vec2 texelSize = vec2(pc.texelSizeX, pc.texelSizeY);

    // 13-tap downsample (standard dual-filter approach)
    // Sample pattern: 4 corner boxes + center, weighted
    //
    //  a . b . c
    //  . d . e .
    //  f . g . h
    //  . i . j .
    //  k . l . m

    vec3 a = texture(inputTexture, texCoord + texelSize * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(inputTexture, texCoord + texelSize * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(inputTexture, texCoord + texelSize * vec2( 2.0, -2.0)).rgb;

    vec3 d = texture(inputTexture, texCoord + texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(inputTexture, texCoord + texelSize * vec2( 1.0, -1.0)).rgb;

    vec3 f = texture(inputTexture, texCoord + texelSize * vec2(-2.0,  0.0)).rgb;
    vec3 g = texture(inputTexture, texCoord).rgb;
    vec3 h = texture(inputTexture, texCoord + texelSize * vec2( 2.0,  0.0)).rgb;

    vec3 i = texture(inputTexture, texCoord + texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 j = texture(inputTexture, texCoord + texelSize * vec2( 1.0,  1.0)).rgb;

    vec3 k = texture(inputTexture, texCoord + texelSize * vec2(-2.0,  2.0)).rgb;
    vec3 l = texture(inputTexture, texCoord + texelSize * vec2( 0.0,  2.0)).rgb;
    vec3 m = texture(inputTexture, texCoord + texelSize * vec2( 2.0,  2.0)).rgb;

    vec3 result;

    if (pc.isFirstPass == 1u)
    {
        // First pass: use Karis average to prevent fireflies
        // Group into 5 sample boxes and weight by Karis
        vec3 g0 = (a + b + f + g) * 0.25;
        vec3 g1 = (b + c + g + h) * 0.25;
        vec3 g2 = (f + g + k + l) * 0.25;
        vec3 g3 = (g + h + l + m) * 0.25;
        vec3 g4 = (d + e + i + j) * 0.25;

        float w0 = karisWeight(g0);
        float w1 = karisWeight(g1);
        float w2 = karisWeight(g2);
        float w3 = karisWeight(g3);
        float w4 = karisWeight(g4);

        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4)
               / (w0 + w1 + w2 + w3 + w4 + 1e-4);

        result = prefilter(result);
    }
    else
    {
        // Subsequent passes: standard 13-tap weighted downsample
        result = (d + e + i + j) * 0.5 * 0.25;
        result += (a + b + g + f) * 0.125 * 0.25;
        result += (b + c + h + g) * 0.125 * 0.25;
        result += (f + g + l + k) * 0.125 * 0.25;
        result += (g + h + m + l) * 0.125 * 0.25;
    }

    outColor = vec4(result, 1.0);
}
