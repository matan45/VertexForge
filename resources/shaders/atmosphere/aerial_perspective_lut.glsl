#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "atmosphere_common.glsl"

layout(rgba16f, set = 0, binding = 0) uniform writeonly image3D aerialPerspectiveLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;
layout(set = 0, binding = 2) uniform sampler2D multiScatterLUT;

layout(std140, set = 0, binding = 3) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

vec3 sampleMultiScatter(float altitude, float cosZenith)
{
    float pR = params.planetParams.x;
    float aR = params.planetParams.y;
    vec2 uv = vec2((cosZenith + 1.0) * 0.5,
                   altitude / (aR - pR));
    return texture(multiScatterLUT, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}

void main()
{
    ivec3 texelCoord = ivec3(gl_GlobalInvocationID.xyz);
    ivec3 lutSize = imageSize(aerialPerspectiveLUT);

    if (texelCoord.x >= lutSize.x || texelCoord.y >= lutSize.y || texelCoord.z >= lutSize.z)
        return;

    float pR = params.planetParams.x;
    float aR = params.planetParams.y;

    vec2 screenUV = (vec2(texelCoord.xy) + 0.5) / vec2(lutSize.xy);
    float depthFrac = (float(texelCoord.z) + 0.5) / float(lutSize.z);

    // Reconstruct view ray from screen UV (flip Y for Vulkan)
    vec2 ndc = vec2(screenUV.x * 2.0 - 1.0, 1.0 - screenUV.y * 2.0);
    vec4 clipFar  = params.invViewProjection * vec4(ndc, 0.0, 1.0);
    vec4 clipNear = params.invViewProjection * vec4(ndc, 1.0, 1.0);
    vec3 viewDir = normalize(clipFar.xyz / clipFar.w - clipNear.xyz / clipNear.w);

    float maxDist = params.screenParams.z; // aerialMaxDist
    float rayDist = depthFrac * depthFrac * maxDist;

    float altitude = params.cameraPosition.w;
    float r = pR + altitude;
    vec3 sunDir = normalize(params.sunDirection.xyz);
    float cosTheta = dot(viewDir, sunDir);

    const int NUM_STEPS = 16;
    float ds = rayDist / float(NUM_STEPS);

    vec3 luminance = vec3(0.0);
    vec3 transmittance = vec3(1.0);

    for (int i = 0; i < NUM_STEPS; ++i)
    {
        float t = (float(i) + 0.5) * ds;
        vec3 samplePos = vec3(0.0, r, 0.0) + viewDir * t;
        float sampleR = length(samplePos);
        float sampleAlt = sampleR - pR;

        if (sampleAlt < 0.0) break;

        float densityR = rayleighDensity(sampleAlt, params.rayleighScattering.w);
        float densityM = mieDensity(sampleAlt, params.mieParams.w);
        float densityO = ozoneDensity(sampleAlt, params.ozoneAbsorption.w, params.ozoneParams.x);

        vec3 sigmaS_R = params.rayleighScattering.xyz * densityR;
        float sigmaS_M = params.mieParams.x * densityM;
        vec3 sigmaT = params.rayleighScattering.xyz * densityR
                    + (params.mieParams.x + params.mieParams.y) * densityM
                    + params.ozoneAbsorption.xyz * densityO;

        vec3 stepTrans = exp(-sigmaT * ds);

        float sunCosZenith = dot(normalize(samplePos), sunDir);
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT, pR, aR, sampleAlt, sunCosZenith);

        vec3 scatterSingle = transToSun * (sigmaS_R * rayleighPhase(cosTheta)
                            + sigmaS_M * miePhase(cosTheta, params.mieParams.z));

        vec3 scatterMulti = sampleMultiScatter(sampleAlt, sunCosZenith)
                          * (sigmaS_R + sigmaS_M);

        vec3 scattering = (scatterSingle + scatterMulti) * params.sunIrradiance.xyz;
        vec3 integral = (scattering - scattering * stepTrans) / max(sigmaT, vec3(0.000001));

        luminance += transmittance * integral;
        transmittance *= stepTrans;
    }

    float avgTrans = dot(transmittance, vec3(1.0 / 3.0));
    imageStore(aerialPerspectiveLUT, texelCoord, vec4(luminance, avgTrans));
}
