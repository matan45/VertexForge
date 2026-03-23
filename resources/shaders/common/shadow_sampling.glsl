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

    // Clamp pageUV inward by 1 texel to avoid sampling the very edge of a
    // physical tile. At page boundaries the outermost texels may hold
    // stale / cleared depth, which shows up as visible seam lines between
    // adjacent tiles. Combined with the CPU-side guard band (which renders
    // overlapping depth into that border), this eliminates boundary seams.
    float borderTexel = 1.0 / PAGE_SIZE_F;
    pageUV = clamp(pageUV, vec2(borderTexel), vec2(1.0 - borderTexel));

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
// Directional Shadow (Clipmap with level selection + blending)
// Reuses sampleVSMShadow — per-level bias is applied on CPU side.
// ============================================================

// Blend in outer 40% of each level to avoid popping at level transitions
const float CLIPMAP_BLEND_START = 0.6;  // fract > this → blend toward next level
const float CLIPMAP_BLEND_END   = 0.4;  // fract < this → blend toward prev level
const float CLIPMAP_FADE_START  = 0.8;  // fraction of max extent where distance fade begins

float sampleDirectionalClipmapShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float worldDist) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int levelCount = int(SHADOW_BUFFER[baseShadowIndex].rangeParams.z);
    levelCount = clamp(levelCount, 1, 16);

    if (baseShadowIndex + levelCount > MAX_SHADOW_VIEWS) {
        levelCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (levelCount <= 0) return 1.0;
    }

    float baseExtent = SHADOW_BUFFER[baseShadowIndex].rangeParams.x;
    if (baseExtent <= 0.0) baseExtent = 2.0;

    // Use world-space distance (rotation-invariant) instead of camera-space viewZ
    // This prevents level selection from changing when the camera rotates
    float continuousLevel = max(log2(max(worldDist, baseExtent) / baseExtent), 0.0);
    int levelIdx = clamp(int(continuousLevel), 0, levelCount - 1);
    int shadowIndex = baseShadowIndex + levelIdx;

    float shadow = sampleVSMShadow(shadowIndex, worldPos, worldNormal);

    // Symmetric blending at level boundaries to prevent popping
    float levelFrac = fract(continuousLevel);
    if (levelFrac > CLIPMAP_BLEND_START && levelIdx < levelCount - 1) {
        float nextShadow = sampleVSMShadow(shadowIndex + 1, worldPos, worldNormal);
        shadow = mix(shadow, nextShadow, smoothstep(CLIPMAP_BLEND_START, 1.0, levelFrac));
    }
    if (levelFrac < CLIPMAP_BLEND_END && levelIdx > 0) {
        float prevShadow = sampleVSMShadow(shadowIndex - 1, worldPos, worldNormal);
        shadow = mix(shadow, prevShadow, smoothstep(CLIPMAP_BLEND_END, 0.0, levelFrac));
    }

    float maxExtent = baseExtent * exp2(float(levelCount - 1));
    float fadeFactor = 1.0 - smoothstep(maxExtent * CLIPMAP_FADE_START, maxExtent, worldDist);
    return mix(1.0, shadow, fadeFactor);
}

// ============================================================
// Unified Directional Shadow Dispatch (cascade or clipmap)
// ============================================================
float sampleDirectionalShadowAuto(int baseShadowIndex, int shadowMode, vec3 worldPos, vec3 worldNormal, float viewZ, vec3 cameraPos) {
    if (shadowMode == 1) {
        float worldDist = length(worldPos - cameraPos);
        return sampleDirectionalClipmapShadow(baseShadowIndex, worldPos, worldNormal, worldDist);
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
