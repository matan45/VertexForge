// VK-1569 — Cubemap-input irradiance convolution (cosine-weighted hemisphere integral).
// Same math as equirectangular_convolution.glsl, but the source is a samplerCube (the dynamic
// sky env cube) instead of an equirectangular sampler2D, and per-face viewProj comes via a
// push constant (so all six faces can record into one command buffer without a UBO race).

#type VERTEX
#version 460 core

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 WorldPos;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

void main()
{
    WorldPos = position;
    gl_Position = pc.viewProj * vec4(position, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec3 WorldPos;

layout(set = 0, binding = 0) uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(WorldPos);

    vec3 irradiance = vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float sampleDelta = 0.025;
    float nrSamples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // Spherical to cartesian (tangent space) then tangent -> world.
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));

    FragColor = vec4(irradiance, 1.0);
}
