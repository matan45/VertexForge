#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

// Fog noise texture generation compute shader
// Generates a 64^3 tileable 3D noise texture (RGBA8)
// R: Perlin-Worley (primary shape), G,B: Worley octaves, A: FBM combination

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(set = 0, binding = 0, rgba8) uniform writeonly image3D fogNoise;

#include "../cloud/cloud_common.glsl"

void main()
{
    ivec3 texel = ivec3(gl_GlobalInvocationID.xyz);
    if (any(greaterThanEqual(texel, ivec3(64))))
        return;

    vec3 p = vec3(texel) / 64.0;

    // R: Perlin-Worley hybrid (low frequency shape)
    float pw = perlinWorley(p, 4.0);

    // G,B: Worley at increasing frequencies
    float w1 = 1.0 - worleyNoise3D(p * 8.0);
    float w2 = 1.0 - worleyNoise3D(p * 16.0);

    // A: FBM combination for direct sampling
    float fbm = pw * 0.5 + w1 * 0.35 + w2 * 0.15;

    imageStore(fogNoise, texel, vec4(pw, w1, w2, fbm));
}
