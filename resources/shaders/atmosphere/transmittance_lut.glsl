#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "atmosphere_common.glsl"

layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D transmittanceLUT;

layout(std140, set = 0, binding = 1) uniform AtmosphereParams {
    float planetRadius;
    float atmosphereRadius;
    float pad0[2];

    vec4 rayleighScattering;  // xyz = scattering, w = densityExpScale

    float mieScattering;
    float mieAbsorption;
    float mieAnisotropy;
    float mieDensityExpScale;

    vec4 ozoneAbsorption;     // xyz = absorption, w = centerAlt
    float ozoneWidth;
    float pad1[3];

    vec4 sunIrradiance;
    vec4 sunDirection;
    vec4 groundAlbedo;
    vec4 cameraPosition;

    mat4 invViewProjection;
    mat4 viewProjection;

    float nearPlane;
    float farPlane;
    float aerialMaxDist;
    float aerialIntensity;

    uint screenWidth;
    uint screenHeight;
    float pad2[2];
} params;

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lutSize = imageSize(transmittanceLUT);

    if (texelCoord.x >= lutSize.x || texelCoord.y >= lutSize.y)
        return;

    vec2 uv = (vec2(texelCoord) + 0.5) / vec2(lutSize);

    float altitude, cosZenith;
    transmittanceLUTUVToParams(params.planetRadius, params.atmosphereRadius,
                                uv, altitude, cosZenith);

    const int NUM_SAMPLES = 40;
    vec3 opticalDepth = computeOpticalDepth(
        params.planetRadius, params.atmosphereRadius,
        altitude, cosZenith,
        params.rayleighScattering.xyz, params.rayleighScattering.w,
        params.mieScattering, params.mieAbsorption, params.mieDensityExpScale,
        params.ozoneAbsorption.xyz, params.ozoneAbsorption.w, params.ozoneWidth,
        NUM_SAMPLES);

    vec3 transmittance = exp(-opticalDepth);

    imageStore(transmittanceLUT, texelCoord, vec4(transmittance, 1.0));
}
