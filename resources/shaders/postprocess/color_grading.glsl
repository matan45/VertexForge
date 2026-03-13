#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;
layout(set = 1, binding = 0) uniform sampler3D primaryLut;
layout(set = 1, binding = 1) uniform sampler3D secondaryLut;

layout(set = 1, binding = 2) uniform ColorGradingParams {
    vec4 lift;
    vec4 gamma;
    vec4 gain;
    float saturation;
    float colorTemperature;
    float colorTint;
    float lutIntensity;
    float lutBlendFactor;
    float hasSecondaryLut;
    float lutSize;
    float pad;
} params;

// Attempt to approximate white balance from color temperature (Kelvin)
// Based on Tanner Helland's approximation of the Planckian locus
vec3 colorTemperatureToRGB(float temperatureKelvin)
{
    float temp = clamp(temperatureKelvin, 1000.0, 15000.0) / 100.0;
    vec3 rgb;

    // Red
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = clamp(1.29293618606 * pow(temp - 60.0, -0.1332047592), 0.0, 1.0);

    // Green
    if (temp <= 66.0)
        rgb.g = clamp(0.39008157876 * log(temp) - 0.63184144378, 0.0, 1.0);
    else
        rgb.g = clamp(1.12989086090 * pow(temp - 60.0, -0.0755148492), 0.0, 1.0);

    // Blue
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = clamp(0.54320678911 * log(temp - 10.0) - 1.19625408914, 0.0, 1.0);

    return rgb;
}

vec3 applyWhiteBalance(vec3 color, float temperature, float tint)
{
    // Compute multiplier relative to D65 (6500K)
    vec3 targetWB = colorTemperatureToRGB(temperature);
    vec3 refWB = colorTemperatureToRGB(6500.0);
    vec3 multiplier = refWB / max(targetWB, vec3(0.001));

    // Apply tint on green-magenta axis
    multiplier.g *= 1.0 + tint * 0.5;

    return color * multiplier;
}

vec3 applyLiftGammaGain(vec3 color, vec3 lift, vec3 gamma, vec3 gain)
{
    // ASC CDL style
    color = gain * (color + lift * (1.0 - color));
    color = max(color, vec3(0.0));
    color = pow(color, 1.0 / max(gamma, vec3(0.01)));
    return color;
}

vec3 applySaturation(vec3 color, float saturation)
{
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, saturation);
}

vec3 sampleLUT(sampler3D lut, vec3 color, float lutSize)
{
    // Map color [0,1] to LUT coordinates with half-texel offset
    vec3 texCoord3D = (color * (lutSize - 1.0) + 0.5) / lutSize;
    return texture(lut, texCoord3D).rgb;
}

void main()
{
    vec3 color = texture(inputTexture, texCoord).rgb;
    vec3 preLutColor = color;

    // White balance
    color = applyWhiteBalance(color, params.colorTemperature, params.colorTint);

    // Lift/Gamma/Gain
    color = applyLiftGammaGain(color, params.lift.rgb, params.gamma.rgb, params.gain.rgb);

    // Saturation
    color = applySaturation(color, params.saturation);

    // Clamp before LUT lookup
    color = clamp(color, 0.0, 1.0);

    // LUT lookup
    vec3 lutColor = sampleLUT(primaryLut, color, params.lutSize);

    // Blend with secondary LUT if available
    if (params.hasSecondaryLut > 0.5)
    {
        vec3 secondaryColor = sampleLUT(secondaryLut, color, params.lutSize);
        lutColor = mix(lutColor, secondaryColor, params.lutBlendFactor);
    }

    // Mix LUT result with pre-LUT color by intensity
    color = mix(preLutColor, lutColor, params.lutIntensity);

    outColor = vec4(color, 1.0);
}
