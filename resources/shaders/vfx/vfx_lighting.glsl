#ifndef VFX_LIGHTING_GLSL
#define VFX_LIGHTING_GLSL

// Simplified lighting evaluation for VFX particles.
// Uses Lambertian diffuse with 1/PI normalization (no PBR metallic/roughness).
//
// Prerequisites: the including shader must declare these BEFORE including this file:
//   - lighting_functions.glsl and cluster_culling.glsl includes
//   - Descriptor set 1: directionalLights[], pointLights[], spotLights[], lightCounts
//   - Descriptor set 2: clusterParams
//   - Descriptor set 3: clusterLightGrid[], lightIndexList[]

// Normal mode constants
const uint VFX_NORMAL_SPHERE = 0u;
const uint VFX_NORMAL_VIEW_ALIGNED = 1u;
const uint VFX_NORMAL_MESH = 2u;

const float VFX_INV_PI = 0.31830988;

// Compute sphere normal from billboard UV (makes particle look spherical)
vec3 computeSphereNormal(vec2 uv, mat4 viewMatrix) {
    vec2 centered = uv * 2.0 - 1.0;
    float lenSq = dot(centered, centered);

    vec3 right   = vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    vec3 up      = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    vec3 forward = -vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);

    if (lenSq > 1.0) {
        return forward;
    }

    float z = sqrt(1.0 - lenSq);
    return normalize(right * centered.x + up * centered.y + forward * z);
}

// Evaluate scene lighting on a VFX particle.
// Returns the lit color (mixed with baseColor by lightingInfluence).
vec3 evaluateVFXLighting(
    vec3 baseColor,
    vec3 normal,
    vec3 worldPos,
    float viewDepth,
    float ambientAmount,
    float lightingInfluence)
{
    vec3 litColor = baseColor * ambientAmount;

    // Directional lights (Lambertian diffuse with 1/PI normalization)
    for (uint i = 0u; i < lightCounts.directionalCount && i < 4u; i++) {
        float NdotL = max(dot(normal, -directionalLights[i].direction), 0.0);
        litColor += baseColor * directionalLights[i].color *
                    directionalLights[i].intensity * NdotL * VFX_INV_PI;
    }

    // Clustered point lights
    uint clusterIdx = getClusterIndex(clusterParams, gl_FragCoord.xy, viewDepth);
    ClusterLightData cluster = clusterLightGrid[clusterIdx];
    uint pointCount = getClusterPointLightCount(cluster);
    uint spotCount = getClusterSpotLightCount(cluster);

    uint maxPts = min(pointCount, 8u);
    for (uint i = 0u; i < maxPts; i++) {
        uint lightIdx = lightIndexList[cluster.offset + i] & LIGHT_INDEX_MASK;
        PointLight light = pointLights[lightIdx];

        vec3 L = light.position - worldPos;
        float dist = length(L);
        if (dist > light.radius) continue;
        L /= dist;

        float NdotL = max(dot(normal, L), 0.0);
        float atten = physicalAttenuation(dist, light.radius);
        litColor += baseColor * light.color * light.intensity * NdotL * atten * VFX_INV_PI;
    }

    // Clustered spot lights
    uint maxSpts = min(spotCount, 4u);
    for (uint i = 0u; i < maxSpts; i++) {
        uint lightIdx = lightIndexList[cluster.offset + pointCount + i] & LIGHT_INDEX_MASK;
        SpotLight light = spotLights[lightIdx];

        vec3 L = light.position - worldPos;
        float dist = length(L);
        if (dist > light.range) continue;
        L /= dist;

        float NdotL = max(dot(normal, L), 0.0);
        float atten = physicalAttenuation(dist, light.range);
        float spotAtten = spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
        litColor += baseColor * light.color * light.intensity * NdotL * atten * spotAtten * VFX_INV_PI;
    }

    return mix(baseColor, litColor, lightingInfluence);
}

#endif // VFX_LIGHTING_GLSL
