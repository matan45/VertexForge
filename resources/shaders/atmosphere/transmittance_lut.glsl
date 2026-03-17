#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "atmosphere_common.glsl"

layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D transmittanceLUT;

layout(std140, set = 0, binding = 1) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lutSize = imageSize(transmittanceLUT);

    if (texelCoord.x >= lutSize.x || texelCoord.y >= lutSize.y)
        return;

    vec2 uv = (vec2(texelCoord) + 0.5) / vec2(lutSize);

    float altitude, cosZenith;
    transmittanceLUTUVToParams(params.planetParams.x, params.planetParams.y,
                                uv, altitude, cosZenith);

    const int NUM_SAMPLES = 40;
    vec3 opticalDepth = computeOpticalDepth(
        params.planetParams.x, params.planetParams.y,
        altitude, cosZenith,
        params.rayleighScattering.xyz, params.rayleighScattering.w,
        params.mieParams.x, params.mieParams.y, params.mieParams.w,
        params.ozoneAbsorption.xyz, params.ozoneAbsorption.w, params.ozoneParams.x,
        NUM_SAMPLES);

    vec3 transmittance = exp(-opticalDepth);

    imageStore(transmittanceLUT, texelCoord, vec4(transmittance, 1.0));
}
