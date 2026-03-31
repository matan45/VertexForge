#ifndef SHADOW_SAMPLING_GLSL
#define SHADOW_SAMPLING_GLSL

#include "shadow_sampling_types.glsl"

// ============================================================
// Requires before #include:
//   #define SHADOW_BUFFER <name>     e.g. shadowData
//   Sampler declarations for:
//     sampler2DShadow  physicalPoolShadow    (binding 0)
//     sampler2D        physicalPoolDepth     (binding 1)
// ============================================================

const int MAX_SHADOW_VIEWS = 272;

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
    // Clamp UV to valid range
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
// Penumbra Estimation
// ============================================================
float estimatePenumbra(float receiverDepth, float avgBlockerDepth, float lightSize) {
    // Quantize receiver depth to discrete steps to stabilize penumbra calculation.
    // Without this, sub-texel floating-point drift in receiverDepth causes the
    // penumbra width to fluctuate frame-to-frame ("breathing" shadow edges).
    const float depthSteps = 4096.0;
    float stableDepth = round(receiverDepth * depthSteps) / depthSteps;
    float stableBlocker = round(avgBlockerDepth * depthSteps) / depthSteps;

    float penumbra = lightSize * (stableDepth - stableBlocker) / max(stableBlocker, 0.0001);
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
// VSM Shadow Sampling (Spot + Directional cascade)
// ============================================================
float sampleVSMShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = SHADOW_BUFFER[shadowIndex];

    // Normal bias
    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z * 0.3;
    vec4 lsPos = sd.viewProjection * vec4(biasedPos, 1.0);

    // For perspective projections (spot/point), reject behind-camera pixels
    bool isPerspective = (sd.pageTableInfo.w == 1 || sd.pageTableInfo.w == 2);
    if (isPerspective && lsPos.w <= 0.0) return 1.0;

    float w = max(lsPos.w, 0.0001);
    vec3 ndc = lsPos.xyz / w;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    float receiverDepth = clamp(ndc.z, 0.0, 1.0);

    // For spot lights, reject pixels outside frustum
    // For point lights (type 2), clamp instead — face selection guarantees correct hemisphere
    if (sd.pageTableInfo.w == 1) {
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    }
    if (sd.pageTableInfo.w == 2) {
        uv = clamp(uv, vec2(0.001), vec2(0.999));
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
// Point Light Shadow (VSM page-based, 6 faces)
// ============================================================
float samplePointShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, vec3 lightPos, float lightRadius) {
    if (baseShadowIndex < 0 || baseShadowIndex + 5 >= MAX_SHADOW_VIEWS) return 1.0;

    // Determine which cube face to sample based on dominant axis
    vec3 lightToFrag = worldPos - lightPos;
    float dist = length(lightToFrag);

    ShadowData sd0 = SHADOW_BUFFER[baseShadowIndex];
    float far = sd0.rangeParams.y;
    if (dist >= far) return 1.0;

    vec3 absDir = abs(lightToFrag);
    int faceIndex;
    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
        faceIndex = (lightToFrag.x > 0.0) ? 0 : 1;
    else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
        faceIndex = (lightToFrag.y > 0.0) ? 2 : 3;
    else
        faceIndex = (lightToFrag.z > 0.0) ? 4 : 5;

    int shadowIndex = baseShadowIndex + faceIndex;
    return sampleVSMShadow(shadowIndex, worldPos, worldNormal);
}

#endif // SHADOW_SAMPLING_GLSL
