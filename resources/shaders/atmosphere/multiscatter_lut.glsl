#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "atmosphere_common.glsl"

layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D multiScatterLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;

layout(std140, set = 0, binding = 2) uniform AtmosphereParams {
#include "atmosphere_params.glsl"
} params;

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lutSize = imageSize(multiScatterLUT);

    if (texelCoord.x >= lutSize.x || texelCoord.y >= lutSize.y)
        return;

    vec2 uv = (vec2(texelCoord) + 0.5) / vec2(lutSize);

    float pR = params.planetParams.x;
    float aR = params.planetParams.y;

    // Map UV to (cosZenith, altitude)
    float cosZenith = uv.x * 2.0 - 1.0;
    float altitude = uv.y * (aR - pR);

    float r = pR + altitude;

    const int SAMPLE_COUNT = 64;
    const float INV_SAMPLES = 1.0 / float(SAMPLE_COUNT);

    vec3 lumTotal = vec3(0.0);
    vec3 fms = vec3(0.0);

    for (int i = 0; i < SAMPLE_COUNT; ++i)
    {
        float phi = 2.0 * PI * fract(float(i) * 0.6180339887);
        float cosTheta = 1.0 - 2.0 * (float(i) + 0.5) * INV_SAMPLES;
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

        vec3 sampleDir = vec3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));

        float t0, t1;
        bool hitAtmo = raySphereIntersect(vec3(0.0, r, 0.0), sampleDir,
                                           vec3(0.0), aR, t0, t1);
        if (!hitAtmo) continue;

        float maxDist = t1;

        float tGround0, tGround1;
        bool hitGround = raySphereIntersect(vec3(0.0, r, 0.0), sampleDir,
                                             vec3(0.0), pR, tGround0, tGround1);
        if (hitGround && tGround0 > 0.0)
            maxDist = min(maxDist, tGround0);

        const int MARCH_STEPS = 20;
        float ds = maxDist / float(MARCH_STEPS);

        vec3 scatterSum = vec3(0.0);
        vec3 transmittance = vec3(1.0);

        for (int j = 0; j < MARCH_STEPS; ++j)
        {
            float t = (float(j) + 0.5) * ds;
            vec3 samplePos = vec3(0.0, r, 0.0) + sampleDir * t;
            float sampleR = length(samplePos);
            float sampleAlt = sampleR - pR;

            if (sampleAlt < 0.0) break;

            float densityR = rayleighDensity(sampleAlt, params.rayleighScattering.w);
            float densityM = mieDensity(sampleAlt, params.mieParams.w);
            float densityO = ozoneDensity(sampleAlt, params.ozoneAbsorption.w, params.ozoneParams.x);

            vec3 sigmaS = params.rayleighScattering.xyz * densityR + params.mieParams.x * densityM;
            vec3 sigmaT = params.rayleighScattering.xyz * densityR
                        + (params.mieParams.x + params.mieParams.y) * densityM
                        + params.ozoneAbsorption.xyz * densityO;

            vec3 sampleTrans = exp(-sigmaT * ds);

            float sunCosZ = dot(normalize(samplePos), vec3(0.0, cosZenith, sqrt(max(1.0 - cosZenith * cosZenith, 0.0))));
            vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT, pR, aR, sampleAlt, sunCosZ);

            vec3 scatContrib = sigmaS * (transToSun * transmittance) * ds;
            scatterSum += scatContrib;

            fms += sigmaS * transmittance * ds;

            transmittance *= sampleTrans;
        }

        if (hitGround && tGround0 > 0.0)
        {
            float groundSunCosZ = cosZenith;
            vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT, pR, aR, 0.0, groundSunCosZ);
            scatterSum += transmittance * params.groundAlbedo.xyz * transToSun / PI;
        }

        lumTotal += scatterSum * INV_SAMPLES;
        fms *= INV_SAMPLES;
    }

    vec3 multiScatter = lumTotal / max(vec3(1.0) - fms, vec3(0.001));

    imageStore(multiScatterLUT, texelCoord, vec4(multiScatter, 1.0));
}
