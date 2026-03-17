#ifndef SHADOW_SAMPLING_GLSL
#define SHADOW_SAMPLING_GLSL

// Shadow Data Structure (must match GPUShadowData in ShadowTypes.hpp - 128 bytes)
struct ShadowData {
    mat4 viewProjection;
    vec4 atlasViewport;
    vec4 biasParams;    // x=depthBias, y=slopeBias, z=normalBias, w=texelSize
    vec4 rangeParams;   // x=near, y=far, z=cascadeCount, w=cascadeIndex
    vec4 pcssParams;    // x=lightSize, y=searchRadius, z=filterEnabled, w=cubeMapIndex
};

// ============================================================
// Requires before #include:
//   #define SHADOW_BUFFER <name>     e.g. shadowData
//   Sampler declarations for:
//     sampler2DShadow  shadowAtlas          (binding 0)
//     sampler2DArrayShadow shadowCascades   (binding 1)
//     samplerCubeShadow shadowCubes[]       (binding 2)
//     sampler2D  shadowAtlasDepth           (binding 3)
//     sampler2DArray shadowCascadesDepth    (binding 4)
//     samplerCube shadowCubesDepth[]        (binding 5)
// ============================================================

const int MAX_SHADOW_VIEWS = 272;
const int MAX_POINT_SHADOW_CUBES = 32;

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
// PCSS Blocker Search - 2D Atlas
// ============================================================
vec2 blockerSearch2D(vec2 uv, float receiverDepth, float searchRadius, vec4 viewport) {
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec2 offset = poissonDisk[i] * searchRadius;
        vec2 sampleUV = uv + offset;

        // Clamp to atlas viewport
        sampleUV = clamp(sampleUV, viewport.xy, viewport.xy + viewport.zw);

        float depth = texture(shadowAtlasDepth, sampleUV).r;
        if (depth < receiverDepth) {
            blockerSum += depth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
        return vec2(-1.0, 0.0); // No blockers

    return vec2(blockerSum / float(blockerCount), float(blockerCount));
}

// ============================================================
// PCSS Blocker Search - Cascade Array
// ============================================================
vec2 blockerSearchCascade(vec2 uv, float layer, float receiverDepth, float searchRadius) {
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec2 offset = poissonDisk[i] * searchRadius;
        vec2 sampleUV = clamp(uv + offset, 0.0, 1.0);

        float depth = texture(shadowCascadesDepth, vec3(sampleUV, layer)).r;
        if (depth < receiverDepth) {
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
    float blockerSum = 0.0;
    int blockerCount = 0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec3 offset = tangent * poissonDisk[i].x * searchRadius +
                      bitangent * poissonDisk[i].y * searchRadius;
        vec3 offsetDir = normalize(sampleDir + offset);

        float depth = texture(shadowCubesDepth[nonuniformEXT(cubeMapIndex)], offsetDir).r;
        if (depth < receiverDepth) {
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
    return lightSize * (receiverDepth - avgBlockerDepth) / avgBlockerDepth;
}

// ============================================================
// PCSS Filter - 2D Atlas (variable-width Poisson PCF)
// ============================================================
float pcssFilter2D(vec3 projCoords, float penumbraWidth, float texelSize, vec4 viewport) {
    float filterRadius = max(penumbraWidth * texelSize, texelSize);
    float shadow = 0.0;

    for (int i = 0; i < PCSS_SAMPLE_COUNT; ++i) {
        vec2 offset = poissonDisk[i] * filterRadius;
        vec2 sampleUV = clamp(projCoords.xy + offset, viewport.xy, viewport.xy + viewport.zw);
        shadow += texture(shadowAtlas, vec3(sampleUV, projCoords.z));
    }

    return shadow / float(PCSS_SAMPLE_COUNT);
}

// ============================================================
// PCSS Filter - Cubemap (variable-width Poisson)
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
// Spot Light Shadow (PCSS)
// ============================================================
float sampleSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = SHADOW_BUFFER[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.xy = sd.atlasViewport.xy + projCoords.xy * sd.atlasViewport.zw;

    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 1.0;
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0)))) return 1.0;

    bool filterEnabled = sd.pcssParams.z > 0.5;
    if (!filterEnabled) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    // PCSS: blocker search
    float lightSize = sd.pcssParams.x;
    float searchRadius = sd.pcssParams.y;
    vec2 blockerResult = blockerSearch2D(projCoords.xy, projCoords.z, searchRadius, sd.atlasViewport);

    if (blockerResult.x < 0.0) return 1.0; // No blockers - fully lit
    if (blockerResult.y >= float(PCSS_SAMPLE_COUNT)) return 0.0; // All blocked

    // Penumbra estimation and variable filter
    float penumbra = estimatePenumbra(projCoords.z, blockerResult.x, lightSize);
    return pcssFilter2D(projCoords, penumbra, sd.biasParams.w, sd.atlasViewport);
}

// ============================================================
// Cascade Shadow (PCSS) - used by directional lights
// ============================================================
float sampleCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = SHADOW_BUFFER[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);

    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    texCoords = clamp(texCoords, 0.0, 1.0);
    projCoords.xy = sd.atlasViewport.xy + texCoords * sd.atlasViewport.zw;
    projCoords.z = clamp(projCoords.z, 0.0, 1.0);

    bool filterEnabled = sd.pcssParams.z > 0.5;
    if (!filterEnabled) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    // PCSS: blocker search
    float lightSize = sd.pcssParams.x;
    float searchRadius = sd.pcssParams.y;
    vec2 blockerResult = blockerSearch2D(projCoords.xy, projCoords.z, searchRadius, sd.atlasViewport);

    if (blockerResult.x < 0.0) return 1.0;
    if (blockerResult.y >= float(PCSS_SAMPLE_COUNT)) return 0.0;

    float penumbra = estimatePenumbra(projCoords.z, blockerResult.x, lightSize);
    return pcssFilter2D(projCoords, penumbra, sd.biasParams.w, sd.atlasViewport);
}

// ============================================================
// Directional Shadow (CSM with cascade selection + PCSS)
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

    float shadow = sampleCascadeShadow(shadowIndex, worldPos, worldNormal);

    // Cascade blending
    float blendZoneStart = cascadeFar * 0.9;
    if (viewZ > blendZoneStart && cascadeIdx < cascadeCount - 1) {
        float nextShadow = sampleCascadeShadow(shadowIndex + 1, worldPos, worldNormal);
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
// Point Light Shadow (Cubemap PCSS)
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

    // Build tangent frame for sampling offsets
    vec3 tangent = abs(sampleDir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 bitangent = normalize(cross(sampleDir, tangent));
    tangent = normalize(cross(bitangent, sampleDir));

    // PCSS: blocker search
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
