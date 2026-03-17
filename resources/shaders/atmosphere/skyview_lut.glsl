#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "atmosphere_common.glsl"

layout(rgba16f, set = 0, binding = 0) uniform writeonly image2D skyViewLUT;
layout(set = 0, binding = 1) uniform sampler2D transmittanceLUT;
layout(set = 0, binding = 2) uniform sampler2D multiScatterLUT;

layout(std140, set = 0, binding = 3) uniform AtmosphereParams {
    float planetRadius;
    float atmosphereRadius;
    float pad0[2];

    vec4 rayleighScattering;
    float mieScattering;
    float mieAbsorption;
    float mieAnisotropy;
    float mieDensityExpScale;

    vec4 ozoneAbsorption;
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

vec3 sampleMultiScatter(float altitude, float cosZenith)
{
    vec2 uv = vec2((cosZenith + 1.0) * 0.5,
                   altitude / (params.atmosphereRadius - params.planetRadius));
    return texture(multiScatterLUT, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 lutSize = imageSize(skyViewLUT);

    if (texelCoord.x >= lutSize.x || texelCoord.y >= lutSize.y)
        return;

    vec2 uv = (vec2(texelCoord) + 0.5) / vec2(lutSize);

    float altitude = params.cameraPosition.w; // altitude above surface
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 sunDir = normalize(params.sunDirection.xyz);

    // Reconstruct view direction from sky-view UV
    vec3 viewDir = skyViewUVToDirection(uv, up, sunDir, altitude, params.planetRadius);

    float r = params.planetRadius + altitude;
    float cosZenith = dot(viewDir, up);

    // Ray-march through atmosphere
    float t0, t1;
    bool hitAtmo = raySphereIntersect(vec3(0.0, r, 0.0), viewDir,
                                       vec3(0.0), params.atmosphereRadius, t0, t1);
    if (!hitAtmo)
    {
        imageStore(skyViewLUT, texelCoord, vec4(0.0));
        return;
    }

    float maxDist = t1;

    // Check ground hit
    float tGround0, tGround1;
    bool hitGround = raySphereIntersect(vec3(0.0, r, 0.0), viewDir,
                                         vec3(0.0), params.planetRadius, tGround0, tGround1);
    if (hitGround && tGround0 > 0.0)
        maxDist = min(maxDist, tGround0);

    const int NUM_STEPS = 32;
    float ds = maxDist / float(NUM_STEPS);

    vec3 luminance = vec3(0.0);
    vec3 transmittance = vec3(1.0);
    float cosTheta = dot(viewDir, sunDir);

    for (int i = 0; i < NUM_STEPS; ++i)
    {
        float t = (float(i) + 0.5) * ds;
        vec3 samplePos = vec3(0.0, r, 0.0) + viewDir * t;
        float sampleR = length(samplePos);
        float sampleAlt = sampleR - params.planetRadius;

        if (sampleAlt < 0.0) break;

        float densityR = rayleighDensity(sampleAlt, params.rayleighScattering.w);
        float densityM = mieDensity(sampleAlt, params.mieDensityExpScale);
        float densityO = ozoneDensity(sampleAlt, params.ozoneAbsorption.w, params.ozoneWidth);

        vec3 sigmaS_R = params.rayleighScattering.xyz * densityR;
        float sigmaS_M = params.mieScattering * densityM;
        vec3 sigmaT = params.rayleighScattering.xyz * densityR
                    + (params.mieScattering + params.mieAbsorption) * densityM
                    + params.ozoneAbsorption.xyz * densityO;

        vec3 stepTrans = exp(-sigmaT * ds);

        // Transmittance to sun from sample
        float sunCosZenith = dot(normalize(samplePos), sunDir);
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT,
            params.planetRadius, params.atmosphereRadius, sampleAlt, sunCosZenith);

        // Phase-weighted single scattering
        vec3 scatterSingle = transToSun * (sigmaS_R * rayleighPhase(cosTheta)
                            + sigmaS_M * miePhase(cosTheta, params.mieAnisotropy));

        // Multi-scattering (isotropic)
        vec3 scatterMulti = sampleMultiScatter(sampleAlt, sunCosZenith)
                          * (sigmaS_R + sigmaS_M);

        // Analytical integration of in-scattering over step
        vec3 scattering = (scatterSingle + scatterMulti) * params.sunIrradiance.xyz;
        vec3 integral = (scattering - scattering * stepTrans) / max(sigmaT, vec3(0.000001));

        luminance += transmittance * integral;
        transmittance *= stepTrans;
    }

    // Ground contribution
    if (hitGround && tGround0 > 0.0)
    {
        float sunCosZ = dot(up, sunDir); // ground normal = up at center
        vec3 transToSun = sampleTransmittanceLUT(transmittanceLUT,
            params.planetRadius, params.atmosphereRadius, 0.0, max(sunCosZ, 0.0));
        luminance += transmittance * params.groundAlbedo.xyz * transToSun
                   * max(sunCosZ, 0.0) * params.sunIrradiance.xyz / PI;
    }

    imageStore(skyViewLUT, texelCoord, vec4(luminance, 1.0));
}
