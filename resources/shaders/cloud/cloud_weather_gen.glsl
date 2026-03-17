#version 460
#extension GL_GOOGLE_include_directive : require
// Weather map generation compute shader
// Generates 2D weather map (1024x1024, RGBA8)
// R=coverage, G=type, B=precipitation, A=unused

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba8) uniform writeonly image2D weatherMap;

#include "cloud_common.glsl"

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(texel, ivec2(1024))))
        return;

    vec2 uv = vec2(texel) / 1024.0;
    vec3 p = vec3(uv * 20.0, 0.0);

    // Coverage: Perlin FBM at multiple scales
    float coverage = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 5; ++i)
    {
        coverage += perlinNoise3D(p * freq) * amp;
        freq *= 2.0;
        amp *= 0.5;
    }
    coverage = clamp(coverage, 0.0, 1.0);

    // Cloud type: smoother, lower frequency
    float cloudType = perlinNoise3D(p * 0.5 + vec3(100.0));
    cloudType = clamp(cloudType * 0.8 + 0.1, 0.0, 1.0);

    // Precipitation: derived from coverage
    float precipitation = smoothstep(0.6, 0.9, coverage);

    imageStore(weatherMap, texel, vec4(coverage, cloudType, precipitation, 1.0));
}
