#version 460
#extension GL_GOOGLE_include_directive : require
// Cloud noise texture generation compute shader
// Generates:
//   Pass 0: 3D shape noise (128^3, RGBA8) - Perlin-Worley + Worley octaves
//   Pass 1: 3D detail noise (32^3, RGBA8)  - High-freq Worley for edge erosion
//   Pass 2: 2D weather map (1024x1024, RGBA8) - R=coverage, G=type, B=precipitation

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(set = 0, binding = 0, rgba8) uniform writeonly image3D shapeNoise;
layout(set = 0, binding = 1, rgba8) uniform writeonly image3D detailNoise;

layout(push_constant) uniform PushConstants {
    uint pass; // 0=shape, 1=detail, 2=weather
} pc;

#include "cloud_common.glsl"

void generateShapeNoise()
{
    ivec3 texel = ivec3(gl_GlobalInvocationID.xyz);
    if (any(greaterThanEqual(texel, ivec3(128))))
        return;

    vec3 p = vec3(texel) / 128.0;

    // R: Perlin-Worley (low frequency shape)
    float pw = perlinWorley(p, 4.0);

    // G,B,A: Worley at increasing frequencies for detail erosion
    float w1 = 1.0 - worleyNoise3D(p * 8.0);
    float w2 = 1.0 - worleyNoise3D(p * 16.0);
    float w3 = 1.0 - worleyNoise3D(p * 32.0);

    imageStore(shapeNoise, texel, vec4(pw, w1, w2, w3));
}

void generateDetailNoise()
{
    ivec3 texel = ivec3(gl_GlobalInvocationID.xyz);
    if (any(greaterThanEqual(texel, ivec3(32))))
        return;

    vec3 p = vec3(texel) / 32.0;

    // High-frequency Worley for edge erosion
    float w1 = 1.0 - worleyNoise3D(p * 8.0);
    float w2 = 1.0 - worleyNoise3D(p * 16.0);
    float w3 = 1.0 - worleyNoise3D(p * 32.0);

    // FBM combination
    float fbm = w1 * 0.625 + w2 * 0.25 + w3 * 0.125;

    imageStore(detailNoise, texel, vec4(w1, w2, w3, fbm));
}

void main()
{
    if (pc.pass == 0)
        generateShapeNoise();
    else if (pc.pass == 1)
        generateDetailNoise();
}
