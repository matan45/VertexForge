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

    // Large-scale coverage: big cloud formations using low-frequency Worley
    // Worley creates distinct cell-like patches (natural cloud grouping)
    float largeCells = 1.0 - worleyNoise3D(vec3(uv * 3.0, 0.0));    // ~3 big cells
    float medCells   = 1.0 - worleyNoise3D(vec3(uv * 6.0, 0.5));    // ~6 medium cells
    float smallCells  = 1.0 - worleyNoise3D(vec3(uv * 12.0, 1.0));   // finer detail

    // Combine: large blobs with medium/small variation
    float coverage = largeCells * 0.6 + medCells * 0.25 + smallCells * 0.15;

    // Add Perlin for organic edges
    float perlinDetail = perlinNoise3D(vec3(uv * 8.0, 2.0));
    coverage = coverage * 0.8 + perlinDetail * 0.2;

    // Increase contrast: push values away from 0.5
    coverage = smoothstep(0.25, 0.75, coverage);

    // Cloud type: large-scale variation (stratus in some areas, cumulus in others)
    float cloudType = 1.0 - worleyNoise3D(vec3(uv * 2.0 + vec2(50.0), 3.0));
    cloudType = smoothstep(0.3, 0.7, cloudType);

    // Precipitation: derived from high coverage areas
    float precipitation = smoothstep(0.7, 0.95, coverage);

    imageStore(weatherMap, texel, vec4(coverage, cloudType, precipitation, 1.0));
}
