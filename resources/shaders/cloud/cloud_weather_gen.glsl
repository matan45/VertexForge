#type COMPUTE
#version 460 core
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

    // Multi-octave Perlin FBM with large spatial features
    // Low frequency = big cloud formations, high frequency = small details
    float n1 = perlinNoise3D(vec3(uv * 4.0, 0.0));          // very large patches
    float n2 = perlinNoise3D(vec3(uv * 8.0, 1.7));           // medium patches
    float n3 = perlinNoise3D(vec3(uv * 16.0, 3.1));          // small detail
    float n4 = perlinNoise3D(vec3(uv * 32.0, 5.3));          // fine detail

    float coverage = n1 * 0.5 + n2 * 0.25 + n3 * 0.15 + n4 * 0.1;

    // Add Worley for clumpy cloud edges (inverted = peaks at cell centers)
    float worley = 1.0 - worleyNoise3D(vec3(uv * 6.0, 7.0));
    coverage = coverage * 0.6 + worley * 0.4;

    // Boost contrast to create clear sky vs cloudy areas
    // Remap from ~[0.3, 0.7] to [0, 1]
    coverage = clamp((coverage - 0.3) * 2.5, 0.0, 1.0);

    // Cloud type: separate noise layer
    float cloudType = perlinNoise3D(vec3(uv * 3.0 + vec2(42.0, 17.0), 9.0));
    cloudType = clamp(cloudType, 0.0, 1.0);

    // Precipitation: only in thick cloud areas
    float precipitation = smoothstep(0.7, 0.95, coverage);

    imageStore(weatherMap, texel, vec4(coverage, cloudType, precipitation, 1.0));
}
