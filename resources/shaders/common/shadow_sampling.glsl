#ifndef SHADOW_SAMPLING_GLSL
#define SHADOW_SAMPLING_GLSL

#include "shadow_sampling_types.glsl"

// ============================================================
// Requires before #include:
//   #define SHADOW_BUFFER <name>     e.g. shadowData
//   Sampler declarations for:
//     sampler2DShadow  physicalPoolShadow    (binding 0)
//     sampler2D        physicalPoolDepth     (binding 1)
//     samplerCubeShadow shadowCubes[]        (binding 2)
//     samplerCube shadowCubesDepth[]         (binding 3)
// ============================================================

const int MAX_SHADOW_VIEWS = 272;
const int MAX_POINT_SHADOW_CUBES = 32;

// VSM Constants
const uint PAGE_SIZE = 128u;
const uint PHYSICAL_POOL_DIM = 8192u;
const uint PAGE_ENTRY_VALID_BIT = 0x80000000u;
const uint PAGE_ENTRY_X_MASK = 0x3Fu;
const uint PAGE_ENTRY_Y_SHIFT = 6u;
const uint PAGE_ENTRY_Y_MASK = 0x3Fu;
const float POOL_DIM_F = float(PHYSICAL_POOL_DIM);
const float PAGE_SIZE_F = float(PAGE_SIZE);

// 32-sample Poisson disk for PCSS sampling
const int PCSS_SAMPLE_COUNT = 32;
const vec2 poissonDisk[32] = vec2[](
    vec2(-0.9465, -0.1428), vec2(-0.7431,  0.5944), vec2(-0.4490,  0.1400),
    vec2(-0.1596,  0.8917), vec2( 0.0483, -0.6390), vec2( 0.3865,  0.5373),
    vec2(-0.5828, -0.5543), vec2( 0.7440, -0.0125), vec2( 0.1567,  0.1295),
    vec2(-0.3358, -0.8774), vec2( 0.5748,  0.7881), vec2(-0.8075, -0.3264),
    vec2( 0.9259, -0.3580), vec2(-0.1516,  0.4303), vec2( 0.3695, -0.3368),
    vec2(-0.6178,  0.3236), vec2( 0.8120, -0.6570), vec2(-0.2714, -0.3483),
    vec2( 0.5923,  0.2277), vec2(-0.4712,  0.7690), vec2( 0.1874, -0.9303),
    vec2( 0.9502,  0.3050), vec2(-0.9073,  0.2284), vec2( 0.2816,  0.9280),
    vec2(-0.6336, -0.1427), vec2( 0.4862, -0.7428), vec2(-0.0982, -0.1554),
    vec2( 0.7040,  0.5315), vec2(-0.4047, -0.6178), vec2( 0.0955,  0.5551),
    vec2(-0.7975,  0.0159), vec2( 0.3573, -0.0547)
);

// ============================================================
// VSM Page Table Lookup
// ============================================================
vec2 vsmLookupPhysicalUV(ShadowData sd, vec2 uv, out bool valid) {
    // Clamp UV to valid range (matches old atlas clamping behavior)
    uv = clamp(uv, vec2(0.0), vec2(0.999));

    ivec2 pageCoord = ivec2(uv * vec2(sd.pageTableInfo.xy));
    pageCoord = clamp(pageCoord, ivec2(0), sd.pageTableInfo.xy - 1);

    uint entryIdx = uint(sd.pageTableInfo.z) + uint(pageCoord.y * sd.pageTableInfo.x + pageCoord.x);
    uint pageEntry = PAGE_TABLE[entryIdx];

    if ((pageEntry & PAGE_ENTRY_VALID_BIT) == 0u) {
        valid = false;
        return vec2(0.0);
    }

    valid = true;

    uint tileX = pageEntry & PAGE_ENTRY_X_MASK;
    uint tileY = (pageEntry >> PAGE_ENTRY_Y_SHIFT) & PAGE_ENTRY_Y_MASK;

    // UV within the page [0,1]
    vec2 pageUV = fract(uv * vec2(sd.pageTableInfo.xy));

    // Physical UV in the pool texture
    vec2 physicalUV = (vec2(float(tileX), float(tileY)) + pageUV) * (PAGE_SIZE_F / POOL_DIM_F);
    return physicalUV;
}

// ============================================================
// PCSS Blocker Search - VSM Physical Pool
// ============================================================
vec2 blockerSearchVSM(ShadowData sd, vec2 uv, float receiverDepth, float searchRadius) {
    float biasedReceiverDepth = receiverDepth - 0.002;
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec2 offset = poissonDisk[i] * searchRadius;
        vec2 sampleUV = clamp(uv + offset, 0.0, 1.0);

        bool sampleValid;
        vec2 physUV = vsmLookupPhysicalUV(sd, sampleUV, sampleValid);
        if (!sampleValid) continue;

        float depth = texture(physicalPoolDepth, physUV).r;
        if (depth < biasedReceiverDepth) {
            blockerSum += depth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
        return vec2(-1.0, 0.0);

    return vec2(blockerSum / float(blockerCount), float(blockerCount));
}

// ============================================================
// PCSS Blocker Search - Cubemap
// ============================================================
vec2 blockerSearchCube(int cubeMapIndex, vec3 sampleDir, float receiverDepth,
                       float searchRadius, vec3 tangent, vec3 bitangent) {
    float biasedReceiverDepth = receiverDepth - 0.002;
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec3 offset = tangent * poissonDisk[i].x * searchRadius +
                      bitangent * poissonDisk[i].y * searchRadius;
        vec3 offsetDir = normalize(sampleDir + offset);

        float depth = texture(shadowCubesDepth[nonuniformEXT(cubeMapIndex)], offsetDir).r;
        if (depth < biasedReceiverDepth) {
            blockerSum += depth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
        return vec2(-1.0, 0.0);

    return vec2(blockerSum / float(blockerCount), float(blockerCount));
}

// ============================================================
// Penumbra Estimation
// ============================================================
float estimatePenumbra(float receiverDepth, float avgBlockerDepth, float lightSize) {
    float penumbra = lightSize * (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
    return min(penumbra, 30.0);
}

// ============================================================
// PCSS Filter - VSM Physical Pool
// ============================================================
float pcssFilterVSM(ShadowData sd, vec2 uv, float receiverDepth, float penumbraWidth, float texelSize) {
    float filterRadius = max(penumbraWidth * texelSize, texelSize);
    float shadow = 0.0;
    int validSamples = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec2 offset = poissonDisk[i] * filterRadius;
        vec2 sampleUV = clamp(uv + offset, 0.0, 1.0);

        bool sampleValid;
        vec2 physUV = vsmLookupPhysicalUV(sd, sampleUV, sampleValid);
        if (!sampleValid) {
            shadow += 1.0; // unmapped = lit
            ++validSamples;
            continue;
        }

        shadow += texture(physicalPoolShadow, vec3(physUV, receiverDepth));
        ++validSamples;
    }

    return validSamples > 0 ? shadow / float(validSamples) : 1.0;
}

// ============================================================
// PCSS Filter - Cubemap
// ============================================================
float pcssFilterCube(int cubeMapIndex, vec3 sampleDir, float perspectiveDepth,
                     float penumbraWidth, vec3 tangent, vec3 bitangent) {
    float filterRadius = max(penumbraWidth * 0.01, 0.001);
    float shadow = 0.0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec3 offset = tangent * poissonDisk[i].x * filterRadius +
                      bitangent * poissonDisk[i].y * filterRadius;
        vec3 offsetDir = normalize(sampleDir + offset);
        shadow += texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(offsetDir, perspectiveDepth));
    }

    return shadow / float(PCSS_SAMPLE_COUNT);
}

// ============================================================
// VSM Shadow Sampling (Spot + Directional cascade)
// ============================================================
float sampleVSMShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = SHADOW_BUFFER[shadowIndex];

    // Normal bias
    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z * 0.3;
    vec4 lsPos = sd.viewProjection * vec4(biasedPos, 1.0);

    // For orthographic projections (directional lights), w is always 1.0
    // For perspective (spot), reject behind-camera pixels
    if (sd.pageTableInfo.w == 1 && lsPos.w <= 0.0) return 1.0;

    float w = max(lsPos.w, 0.0001);
    vec3 ndc = lsPos.xyz / w;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    float receiverDepth = clamp(ndc.z, 0.0, 1.0);

    // For spot lights, reject pixels outside frustum
    if (sd.pageTableInfo.w == 1) {
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    }

    // Look up page table
    bool valid;
    vec2 physicalUV = vsmLookupPhysicalUV(sd, uv, valid);
    if (!valid) return 1.0; // unmapped page = fully lit

    bool filterEnabled = sd.pcssParams.z > 0.5;
    if (!filterEnabled) {
        return texture(physicalPoolShadow, vec3(physicalUV, receiverDepth));
    }

    // PCSS: blocker search
    float lightSize = sd.pcssParams.x;
    float searchRadius = sd.pcssParams.y;
    vec2 blockerResult = blockerSearchVSM(sd, uv, receiverDepth, searchRadius);

    if (blockerResult.x < 0.0) return 1.0;
    if (blockerResult.y >= float(PCSS_SAMPLE_COUNT)) return 0.0;

    float penumbra = estimatePenumbra(receiverDepth, blockerResult.x, lightSize);
    return pcssFilterVSM(sd, uv, receiverDepth, penumbra, sd.biasParams.w);
}

// ============================================================
// Spot Light Shadow
// ============================================================
float sampleSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    return sampleVSMShadow(shadowIndex, worldPos, worldNormal);
}

// ============================================================
// Cascade Shadow (single cascade)
// ============================================================
float sampleCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    return sampleVSMShadow(shadowIndex, worldPos, worldNormal);
}

// ============================================================
// Directional Shadow (CSM with cascade selection)
// ============================================================
float sampleDirectionalShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int cascadeCount = int(SHADOW_BUFFER[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);

    if (baseShadowIndex + cascadeCount > MAX_SHADOW_VIEWS) {
        cascadeCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (cascadeCount <= 0) return 1.0;
    }

    int cascadeIdx = 0;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewZ < SHADOW_BUFFER[baseShadowIndex + i].rangeParams.y) {
            cascadeIdx = i;
            break;
        }
        cascadeIdx = i;
    }

    int shadowIndex = baseShadowIndex + cascadeIdx;
    float cascadeFar = SHADOW_BUFFER[shadowIndex].rangeParams.y;

    float shadow = sampleVSMShadow(shadowIndex, worldPos, worldNormal);

    // Cascade blending
    float blendZoneStart = cascadeFar * 0.9;
    if (viewZ > blendZoneStart && cascadeIdx < cascadeCount - 1) {
        float nextShadow = sampleVSMShadow(shadowIndex + 1, worldPos, worldNormal);
        float blendFactor = smoothstep(blendZoneStart, cascadeFar, viewZ);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    // Distance fade-out
    float maxDistance = SHADOW_BUFFER[baseShadowIndex + cascadeCount - 1].rangeParams.y;
    float fadeStart = maxDistance * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxDistance, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

// ============================================================
// Directional Shadow (Clipmap with level selection)
// ============================================================
float sampleDirectionalClipmapShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int levelCount = int(SHADOW_BUFFER[baseShadowIndex].rangeParams.z);
    levelCount = clamp(levelCount, 1, 16);

    if (baseShadowIndex + levelCount > MAX_SHADOW_VIEWS) {
        levelCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (levelCount <= 0) return 1.0;
    }

    // Level selection: log2(distance / baseExtent), clamped
    float baseExtent = SHADOW_BUFFER[baseShadowIndex].rangeParams.x;
    float level = log2(max(viewZ, baseExtent) / baseExtent);
    int levelIdx = clamp(int(level), 0, levelCount - 1);

    int shadowIndex = baseShadowIndex + levelIdx;
    float shadow = sampleVSMShadow(shadowIndex, worldPos, worldNormal);

    // Inter-level blending at boundaries (blend zone: 70%-100% of level transition)
    float levelFrac = fract(level);
    if (levelFrac > 0.7 && levelIdx < levelCount - 1) {
        float nextShadow = sampleVSMShadow(shadowIndex + 1, worldPos, worldNormal);
        float blend = smoothstep(0.7, 1.0, levelFrac);
        shadow = mix(shadow, nextShadow, blend);
    }

    // Distance fade at outermost level
    float maxExtent = baseExtent * exp2(float(levelCount - 1));
    float fadeStart = maxExtent * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxExtent, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

// ============================================================
// Directional Shadow (auto-dispatch CSM vs Clipmap)
// ============================================================
float sampleDirectionalShadowAuto(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;
    int lightType = SHADOW_BUFFER[baseShadowIndex].pageTableInfo.w;
    if (lightType == 3) {
        return sampleDirectionalClipmapShadow(baseShadowIndex, worldPos, worldNormal, viewZ);
    }
    return sampleDirectionalShadow(baseShadowIndex, worldPos, worldNormal, viewZ);
}

// ============================================================
// Point Light Shadow (Cubemap PCSS - unchanged)
// ============================================================
float samplePointShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal, vec3 lightPos, float lightRadius) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = SHADOW_BUFFER[shadowIndex];

    int cubeMapIndex = int(sd.pcssParams.w);
    if (cubeMapIndex < 0 || cubeMapIndex >= MAX_POINT_SHADOW_CUBES) return 1.0;

    float near = sd.rangeParams.x;
    float far = sd.rangeParams.y;
    vec3 lightToFrag = worldPos - lightPos;
    float linearDepth = length(lightToFrag);

    if (linearDepth >= far) return 1.0;

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    lightToFrag = biasedPos - lightPos;
    linearDepth = length(lightToFrag);
    vec3 sampleDir = normalize(lightToFrag);

    float majorComponent = max(abs(sampleDir.x), max(abs(sampleDir.y), abs(sampleDir.z)));
    float viewSpaceZ = linearDepth * majorComponent;
    float perspectiveDepth = (far * (viewSpaceZ - near)) / (viewSpaceZ * (far - near));

    bool filterEnabled = sd.pcssParams.z > 0.5;
    if (!filterEnabled) {
        return texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(sampleDir, perspectiveDepth));
    }

    // Build tangent frame
    vec3 tangent = abs(sampleDir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 bitangent = normalize(cross(sampleDir, tangent));
    tangent = normalize(cross(bitangent, sampleDir));

    // PCSS
    float lightSize = sd.pcssParams.x;
    float searchRadius = sd.pcssParams.y;
    vec2 blockerResult = blockerSearchCube(cubeMapIndex, sampleDir, perspectiveDepth,
                                           searchRadius, tangent, bitangent);

    if (blockerResult.x < 0.0) return 1.0;
    if (blockerResult.y >= float(PCSS_SAMPLE_COUNT)) return 0.0;

    float penumbra = estimatePenumbra(perspectiveDepth, blockerResult.x, lightSize);
    return pcssFilterCube(cubeMapIndex, sampleDir, perspectiveDepth, penumbra, tangent, bitangent);
}

#endif // SHADOW_SAMPLING_GLSL
