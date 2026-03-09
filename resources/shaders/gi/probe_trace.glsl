#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

#ifdef USE_RAY_QUERY
#extension GL_EXT_ray_query : require
#endif

#include "gi_common.glsl"

#ifdef USE_LIGHT_DATA
#include "../common/lighting_functions.glsl"
#endif

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Set 0: Probe data (read + write)
layout(std430, set = 0, binding = 0) readonly buffer ProbeReadBuffer {
    ProbeData probeDataRead[];
};

layout(std430, set = 0, binding = 1) writeonly buffer ProbeWriteBuffer {
    ProbeData probeDataWrite[];
};

// Set 1: Cascade info
layout(std140, set = 1, binding = 0) uniform CascadeInfoUBO {
    CascadeInfo cascades[8];
};

#ifdef USE_RAY_QUERY
// Set 2: TLAS for ray queries
layout(set = 2, binding = 0) uniform accelerationStructureEXT topLevelAS;
#endif

#ifdef USE_LIGHT_DATA
// Set 3 (or 2 if no ray query): Light data
// Binding layout matches GPULightBufferManager
#ifdef USE_RAY_QUERY
#define LIGHT_SET 3
#else
#define LIGHT_SET 2
#endif

layout(std430, set = LIGHT_SET, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std430, set = LIGHT_SET, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, set = LIGHT_SET, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

layout(std140, set = LIGHT_SET, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};
#endif

layout(push_constant) uniform PushConstants {
    uint cascadeIndex;
    uint probeStartIndex;
    uint probeCount;
    uint raysPerProbe;
    float maxDistance;
    float temporalBlend;
    float frameRandom;
    uint frameIndex;
};

// Hash for random ray generation
float hash31(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 uniformSphereDirection(float u1, float u2) {
    float theta = 2.0 * 3.14159265 * u1;
    float phi = acos(2.0 * u2 - 1.0);
    return vec3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
}

#ifdef USE_LIGHT_DATA
// Evaluate direct lighting at a hit point using a simplified diffuse-only model
// (probes capture diffuse irradiance, no specular needed)
vec3 evaluateDirectLightingAtHitPoint(vec3 hitPos, vec3 hitNormal) {
    vec3 totalLight = vec3(0.0);

    // Directional lights
    for (uint i = 0; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        vec3 L = -normalize(light.direction);
        float NdotL = max(dot(hitNormal, L), 0.0);
        if (NdotL > 0.0) {
            totalLight += light.color * light.intensity * NdotL;
        }
    }

    // Point lights
    for (uint i = 0; i < lightCounts.pointCount; ++i) {
        PointLight light = pointLights[i];
        vec3 toLight = light.position - hitPos;
        float dist = length(toLight);
        if (dist > light.radius) continue;

        vec3 L = toLight / dist;
        float NdotL = max(dot(hitNormal, L), 0.0);
        if (NdotL <= 0.0) continue;

        float attenuation = physicalAttenuation(dist, light.radius);
        totalLight += light.color * light.intensity * attenuation * NdotL;
    }

    // Spot lights
    for (uint i = 0; i < lightCounts.spotCount; ++i) {
        SpotLight light = spotLights[i];
        vec3 toLight = light.position - hitPos;
        float dist = length(toLight);
        if (dist > light.range) continue;

        vec3 L = toLight / dist;
        float spotAtt = spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
        if (spotAtt <= 0.0) continue;

        float NdotL = max(dot(hitNormal, L), 0.0);
        if (NdotL <= 0.0) continue;

        float distAtt = physicalAttenuation(dist, light.range);
        totalLight += light.color * light.intensity * distAtt * spotAtt * NdotL;
    }

    return totalLight;
}
#endif

// Sample multi-bounce irradiance from probes at a world position
vec3 sampleProbeIrradiance(vec3 worldPos, vec3 direction) {
    for (int c = 0; c < 8; ++c) {
        CascadeInfo hitCascade = cascades[c];
        if (hitCascade.gridDimsOffset.w < 0) break;

        vec3 localPos = (worldPos - hitCascade.gridOriginSpacing.xyz) / hitCascade.gridOriginSpacing.w;
        ivec3 gridDims = hitCascade.gridDimsOffset.xyz;

        if (all(greaterThanEqual(localPos, vec3(0.0))) &&
            all(lessThan(localPos, vec3(gridDims) - vec3(1.0)))) {

            ivec3 baseCoord = ivec3(floor(localPos));
            vec3 alpha = fract(localPos);

            vec3 interpRadiance = vec3(0.0);
            float totalWeight = 0.0;

            for (int dz = 0; dz < 2; ++dz) {
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        ivec3 coord = baseCoord + ivec3(dx, dy, dz);
                        coord = clamp(coord, ivec3(0), gridDims - ivec3(1));

                        uint idx = uint(hitCascade.gridDimsOffset.w) +
                                   gridToProbeIndex(coord, gridDims);

                        ProbeData neighbor = probeDataRead[idx];

                        float wx = (dx == 0) ? (1.0 - alpha.x) : alpha.x;
                        float wy = (dy == 0) ? (1.0 - alpha.y) : alpha.y;
                        float wz = (dz == 0) ? (1.0 - alpha.z) : alpha.z;
                        float w = wx * wy * wz * neighbor.validity.x;

                        if (w > 0.001) {
                            interpRadiance += evaluateSH(neighbor, direction) * w;
                            totalWeight += w;
                        }
                    }
                }
            }

            if (totalWeight > 0.001) {
                return interpRadiance / totalWeight;
            }
            break;
        }
    }
    return vec3(0.0);
}

void main() {
    uint localIndex = gl_GlobalInvocationID.x;
    if (localIndex >= probeCount) return;

    uint globalProbeIndex = probeStartIndex + localIndex;

    CascadeInfo cascade = cascades[cascadeIndex];
    uint localProbeIndex = globalProbeIndex - uint(cascade.gridDimsOffset.w);

    vec3 probePos = probeWorldPosition(localProbeIndex, cascade);

    // Accumulate new SH coefficients from ray tracing
    vec4 newShR = vec4(0.0);
    vec4 newShG = vec4(0.0);
    vec4 newShB = vec4(0.0);
    float validHits = 0.0;
    float backfaceHits = 0.0;

    for (uint ray = 0; ray < raysPerProbe; ++ray) {
        // Generate random ray direction (uniform sphere)
        float u1 = hash31(vec3(float(localProbeIndex), float(ray), frameRandom));
        float u2 = hash31(vec3(float(ray), frameRandom, float(localProbeIndex)));

        vec3 rayDir = uniformSphereDirection(u1, u2);

        vec3 radiance = vec3(0.0);
        bool hit = false;

#ifdef USE_RAY_QUERY
        // Hardware ray query against TLAS
        rayQueryEXT rayQuery;
        rayQueryInitializeEXT(rayQuery, topLevelAS,
                              gl_RayFlagsOpaqueEXT,
                              0xFF,
                              probePos,
                              0.05,          // tMin: small offset to avoid self-intersection
                              rayDir,
                              maxDistance);

        while (rayQueryProceedEXT(rayQuery)) {
            // Opaque geometry commits automatically
        }

        if (rayQueryGetIntersectionTypeEXT(rayQuery, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
            hit = true;
            float hitDistance = rayQueryGetIntersectionTEXT(rayQuery, true);
            bool frontFace = rayQueryGetIntersectionFrontFaceEXT(rayQuery, true);

            if (!frontFace) {
                backfaceHits += 1.0;
            }

            vec3 hitPoint = probePos + rayDir * hitDistance;

            // Approximate hit normal from ray direction and front-face info
            // With ray queries we don't get the actual triangle normal unless we
            // store normals in a separate buffer. Use the reversed ray as a proxy
            // normal for diffuse evaluation (Lambertian assumes surface faces the ray).
            vec3 hitNormal = frontFace ? -rayDir : rayDir;

#ifdef USE_LIGHT_DATA
            // === PRIMARY BOUNCE: evaluate direct lighting at the hit point ===
            vec3 directLight = evaluateDirectLightingAtHitPoint(hitPoint, hitNormal);

            // Assume average scene albedo of ~0.5 for diffuse reflection
            // (we don't have per-triangle material data in the TLAS)
            radiance = directLight * 0.5;
#endif

            // === MULTI-BOUNCE: sample previous frame's probe irradiance at hit point ===
            vec3 indirectLight = sampleProbeIrradiance(hitPoint, hitNormal);
            radiance += indirectLight * 0.5; // Attenuate multi-bounce to prevent energy blowup

        }
#else
        // Fallback: no ray query available
#ifdef USE_LIGHT_DATA
        // Without geometry tracing, approximate by evaluating lights at probe position
        // using the ray direction as an assumed surface normal
        vec3 directLight = evaluateDirectLightingAtHitPoint(probePos, rayDir);
        radiance = directLight * 0.3; // Reduced contribution without actual geometry hits
#endif

        // Multi-bounce from own previous irradiance
        ProbeData prevData = probeDataRead[globalProbeIndex];
        if (prevData.validity.x > 0.1) {
            radiance += evaluateSH(prevData, rayDir) * 0.5;
        }
        hit = true;
#endif

        // Accumulate into SH
        accumulateSH(newShR, newShG, newShB, rayDir, radiance);
        if (hit) validHits += 1.0;
    }

    // Normalize: integrate over sphere (4*PI solid angle)
    float invRays = 4.0 * 3.14159265 / max(float(raysPerProbe), 1.0);
    newShR *= invRays;
    newShG *= invRays;
    newShB *= invRays;

    // Temporal blending with previous frame
    ProbeData prevProbe = probeDataRead[globalProbeIndex];
    float blend = temporalBlend;

    // Reduce blend for probes with low validity (fresh probes converge faster)
    if (prevProbe.validity.x < 0.1) {
        blend = 0.0;
    }

    ProbeData result;
    result.shR = mix(newShR, prevProbe.shR, blend);
    result.shG = mix(newShG, prevProbe.shG, blend);
    result.shB = mix(newShB, prevProbe.shB, blend);

    result.validity.x = mix(validHits / max(float(raysPerProbe), 1.0), prevProbe.validity.x, blend);
    result.validity.y = prevProbe.validity.y + 1.0; // Age
    result.validity.z = backfaceHits / max(float(raysPerProbe), 1.0);
    result.validity.w = 0.0;

    probeDataWrite[globalProbeIndex] = result;
}
