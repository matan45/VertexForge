#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D depthTexture;
layout(set = 0, binding = 1) uniform SunData {
    vec2 sunScreenPos;
    float intensity;
    float decay;
    float density;
    float weight;
    int sampleCount;
    float threshold;
} sun;

void main()
{
    // Direction from this pixel toward the sun
    vec2 deltaTexCoord = texCoord - sun.sunScreenPos;
    deltaTexCoord *= (1.0 / float(sun.sampleCount)) * sun.density;

    vec2 sampleUV = texCoord;
    float illumination = 0.0;
    float decayFactor = 1.0;

    for (int i = 0; i < sun.sampleCount; ++i)
    {
        sampleUV -= deltaTexCoord;

        // Clamp to valid UV range
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            break;

        float depth = texture(depthTexture, sampleUV).r;

        // Sky pixels (depth >= threshold) contribute light, geometry blocks it
        float lightSample = step(sun.threshold, depth);

        lightSample *= decayFactor * sun.weight;
        illumination += lightSample;
        decayFactor *= sun.decay;
    }

    illumination /= float(sun.sampleCount);
    illumination *= sun.intensity;

    outColor = vec4(vec3(illumination), 1.0);
}
