#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragOceanDispY;  // ocean wave height displacement
layout(location = 4) out float fragBaseHeight;   // flat water height (no waves)

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

struct WaterTileData {
    vec4 worldOriginAndSize;   // xyz = plane world origin, w = planeSize
    vec4 heightAndWave;        // x = waterHeight, y = 1.0, z = 0, w = 0
};
layout(set = 1, binding = 0) readonly buffer TileBuffer {
    WaterTileData tiles[];
} tileData;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float maxVisibleDepth;
    float fresnelPower;
    float oceanChoppiness;
    float oceanPatchSize0;       // Swell band patch size
    float oceanFoamThreshold;
    float refractionStrength;
    float refractionChromatic;
    float refractionDepthScale;
    float oceanPatchSize1;       // Agitation band patch size
    float oceanPatchSize2;       // Ripples band patch size
    uint  bandEnableMask;        // bit 0=swell, bit 1=agitation, bit 2=ripples
    float shoreFoamRange;
    float shoreFoamIntensity;
    float shoreBreakingStrength;
} pc;

// Multi-band ocean textures (set 8)
layout(set = 8, binding = 0) uniform sampler2D oceanDisp0;   // Swell displacement
layout(set = 8, binding = 1) uniform sampler2D oceanNorm0;   // Swell normals
layout(set = 8, binding = 2) uniform sampler2D oceanDisp1;   // Agitation displacement
layout(set = 8, binding = 3) uniform sampler2D oceanNorm1;   // Agitation normals
layout(set = 8, binding = 4) uniform sampler2D oceanDisp2;   // Ripples displacement
layout(set = 8, binding = 5) uniform sampler2D oceanNorm2;   // Ripples normals

void main() {
    uint tileIndex = gl_InstanceIndex;
    vec4 originSize = tileData.tiles[tileIndex].worldOriginAndSize;
    vec4 heightWave = tileData.tiles[tileIndex].heightAndWave;

    vec3 tileOrigin = originSize.xyz;
    float tileSize = originSize.w;
    float waterHeight = heightWave.x;

    vec3 worldPos = vec3(
        tileOrigin.x + inPosition.x * tileSize,
        waterHeight,
        tileOrigin.z + inPosition.z * tileSize
    );

    fragBaseHeight = waterHeight;

    // Per-tile simulation LOD: skip expensive bands at coarse LODs
    uint lodLevel = uint(heightWave.z);
    uint tileBandMask = pc.bandEnableMask;
    if (lodLevel >= 2u) tileBandMask &= ~4u;  // skip ripples at LOD2+
    if (lodLevel >= 3u) tileBandMask &= ~2u;  // skip agitation at LOD3

    // Multi-band FFT displacement: each band at its own patch size
    vec4 totalDisp = vec4(0.0);
    vec3 totalNorm = vec3(0.0, 1.0, 0.0);

    // Band 0: Swell (large-scale distant wind waves)
    if ((tileBandMask & 1u) != 0u) {
        vec2 uv0 = worldPos.xz / pc.oceanPatchSize0;
        totalDisp += texture(oceanDisp0, uv0);
        totalNorm = texture(oceanNorm0, uv0).xyz;
    }

    // Band 1: Agitation (mid-frequency wind chaos)
    if ((tileBandMask & 2u) != 0u) {
        vec2 uv1 = worldPos.xz / pc.oceanPatchSize1;
        totalDisp += texture(oceanDisp1, uv1);
        vec3 n1 = texture(oceanNorm1, uv1).xyz;
        totalNorm = normalize(totalNorm + (n1 - vec3(0.0, 1.0, 0.0)));
    }

    // Band 2: Ripples (fine surface detail)
    if ((tileBandMask & 4u) != 0u) {
        vec2 uv2 = worldPos.xz / pc.oceanPatchSize2;
        totalDisp += texture(oceanDisp2, uv2);
        vec3 n2 = texture(oceanNorm2, uv2).xyz;
        totalNorm = normalize(totalNorm + (n2 - vec3(0.0, 1.0, 0.0)));
    }

    worldPos.x += totalDisp.x;
    worldPos.y += totalDisp.y;
    worldPos.z += totalDisp.z;

    fragOceanDispY = totalDisp.y;
    fragNormal = normalize(totalNorm);

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragOceanDispY;
layout(location = 4) in float fragBaseHeight;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

#include "../common/lighting_functions.glsl"
#include "../common/cluster_culling.glsl"

layout(std430, set = 3, binding = 0) readonly buffer DirectionalLightBuffer { DirectionalLight directionalLights[]; };
layout(std430, set = 3, binding = 1) readonly buffer PointLightBuffer { PointLight pointLights[]; };
layout(std430, set = 3, binding = 2) readonly buffer SpotLightBuffer { SpotLight spotLights[]; };
layout(std140, set = 3, binding = 3) uniform LightCountsUBO { LightCounts lightCounts; };

layout(std140, set = 4, binding = 0) uniform ClusterParamsUBO { ClusterGridParams clusterParams; };

layout(std430, set = 5, binding = 0) readonly buffer ClusterLightGridBuffer { ClusterLightData clusterLightGrid[]; };
layout(std430, set = 5, binding = 1) readonly buffer ClusterLightIndexListBuffer { uint lightIndexList[]; };

#include "../common/shadow_sampling_types.glsl"

layout(std430, set = 6, binding = 0) readonly buffer ShadowDataBuffer { ShadowData shadowDataArray[]; };

layout(std430, set = 6, binding = 1) readonly buffer PageTableBuffer { uint pageTableData[]; };

// Comparison samplers (shadow filtering)
layout(set = 7, binding = 0) uniform sampler2DShadow physicalPoolShadow;
layout(set = 7, binding = 1) uniform sampler2D physicalPoolDepth;

#define SHADOW_BUFFER shadowDataArray
#define PAGE_TABLE pageTableData
#include "../common/shadow_sampling.glsl"

// Multi-band ocean textures (set 8)
layout(set = 8, binding = 0) uniform sampler2D frag_oceanDisp0;
layout(set = 8, binding = 1) uniform sampler2D frag_oceanNorm0;
layout(set = 8, binding = 2) uniform sampler2D frag_oceanDisp1;
layout(set = 8, binding = 3) uniform sampler2D frag_oceanNorm1;
layout(set = 8, binding = 4) uniform sampler2D frag_oceanDisp2;
layout(set = 8, binding = 5) uniform sampler2D frag_oceanNorm2;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float maxVisibleDepth;
    float fresnelPower;
    float oceanChoppiness;
    float oceanPatchSize0;
    float oceanFoamThreshold;
    float refractionStrength;
    float refractionChromatic;
    float refractionDepthScale;
    float oceanPatchSize1;
    float oceanPatchSize2;
    uint  bandEnableMask;
    float shoreFoamRange;
    float shoreFoamIntensity;
    float shoreBreakingStrength;
} pc;

layout(set = 9, binding = 0) uniform sampler2D refractionColorTex;
layout(set = 9, binding = 1) uniform sampler2D sceneDepthTex;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, pc.fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);

    float roughness = 0.05;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 F0 = vec3(0.02); // Water IOR ~1.33
    vec3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    // Height-based color: wave peaks get shallow color, troughs get deep color
    float heightRange = fragOceanDispY;
    float heightFactor = clamp(heightRange * 0.5 + 0.5, 0.0, 1.0);
    vec3 waterColor = mix(pc.deepColor.rgb, pc.shallowColor.rgb, heightFactor);

    // Subsurface scattering approximation
    vec3 sssColor = vec3(0.0, 0.7, 0.6);
    vec3 L = normalize(vec3(0.5, 0.7, 0.3));
    vec3 lightColor = vec3(1.0);
    if (lightCounts.directionalCount > 0u) {
        L = normalize(-directionalLights[0].direction);
        lightColor = directionalLights[0].color;
    }
    float LdotV = max(dot(L, -V), 0.0);
    float waveHeight = clamp(heightRange, 0.0, 1.0);
    float NdotL = dot(N, L);
    float wrapDiffuse = max(0.0, (NdotL + 0.5) / 1.5);
    float sss = pow(LdotV, 4.0) * waveHeight * 0.8 + wrapDiffuse * waveHeight * 0.3;
    sss = clamp(sss, 0.0, 1.0);
    waterColor = mix(waterColor, sssColor * lightColor, sss);

    fresnel = clamp(fresnel, 0.02, 0.6);

    float metallic = 0.0;
    float directRoughness = 0.3;
    vec3 albedo = waterColor;

    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    float linearZ = linearizeDepth(clusterParams, gl_FragCoord.z);
    uint clusterIdx = getClusterIndex(clusterParams, gl_FragCoord.xy, linearZ);

    if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
        ClusterLightData clusterData = clusterLightGrid[clusterIdx];
        uint clusterPointCount = getClusterPointLightCount(clusterData);
        uint clusterSpotCount = getClusterSpotLightCount(clusterData);
        uint lightOffset = clusterData.offset;

        for (uint i = 0u; i < clusterPointCount; ++i) {
            uint lightIdx = lightIndexList[lightOffset + i];
            PointLight light = pointLights[lightIdx];

            float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N,
                                             light.position, light.radius);
            minShadow = min(minShadow, shadow);

            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo,
                                                 metallic, directRoughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = extractLightIndex(packedIdx);
            SpotLight light = spotLights[lightIdx];

            float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
            minShadow = min(minShadow, shadow);

            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo,
                                                metallic, directRoughness, F0, light) * shadow;
        }
    }

    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];

        float shadow = 1.0;
        if (light.shadowIndex >= 0) {
            shadow = sampleDirectionalShadowAuto(light.shadowIndex, light.shadowMode, fragWorldPos, N, linearZ, camera.cameraPos);
        }
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, directRoughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);

    // Refraction (samples scene color captured before water pass)
    vec3 refractionColor = waterColor;
    if (pc.refractionStrength > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(refractionColorTex, 0));

        float viewAngleFactor = 1.0 - abs(dot(V, vec3(0.0, 1.0, 0.0)));
        float depthFactor = smoothstep(0.0, 1.0, viewAngleFactor * pc.refractionDepthScale);

        vec2 distortion = N.xz * pc.refractionStrength * depthFactor * 0.1;
        vec2 refractedUV = clamp(screenUV + distortion, vec2(0.001), vec2(0.999));

        if (pc.refractionChromatic > 0.0) {
            float spread = pc.refractionChromatic * depthFactor * 0.002;
            vec2 chromaticDir = normalize(distortion + vec2(0.001));
            float r = texture(refractionColorTex, refractedUV + chromaticDir * spread).r;
            float g = texture(refractionColorTex, refractedUV).g;
            float b = texture(refractionColorTex, refractedUV - chromaticDir * spread).b;
            refractionColor = vec3(r, g, b);
        } else {
            refractionColor = texture(refractionColorTex, refractedUV).rgb;
        }

        float depthTint = smoothstep(0.0, 1.0, viewAngleFactor);
        refractionColor = mix(refractionColor, waterColor, depthTint * 0.5);
    }

    vec3 baseColor = (pc.refractionStrength > 0.0) ? refractionColor : waterColor;
    vec3 color = mix(baseColor, specular, fresnel) * ambientShadowFactor + directLighting;

    // Ocean foam blending — sum foam from all active bands
    float foam = 0.0;
    if ((pc.bandEnableMask & 1u) != 0u) {
        vec2 foamUV0 = fragWorldPos.xz / pc.oceanPatchSize0;
        foam += texture(frag_oceanDisp0, foamUV0).w;
    }
    if ((pc.bandEnableMask & 2u) != 0u) {
        vec2 foamUV1 = fragWorldPos.xz / pc.oceanPatchSize1;
        foam += texture(frag_oceanDisp1, foamUV1).w * 0.5;
    }
    if ((pc.bandEnableMask & 4u) != 0u) {
        vec2 foamUV2 = fragWorldPos.xz / pc.oceanPatchSize2;
        foam += texture(frag_oceanDisp2, foamUV2).w * 0.3;
    }

    // Shore foam — depth-based foam where water meets terrain
    if (pc.shoreFoamRange > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTex, 0));
        float terrainDepthRaw = texture(sceneDepthTex, screenUV).r;

        // Reconstruct terrain world position from depth buffer
        float near = clusterParams.depthParams.x;
        float far  = clusterParams.depthParams.y;
        float terrainLinearZ = near * far / max(far - terrainDepthRaw * (far - near), 0.0001);
        float waterLinearZ   = near * far / max(far - gl_FragCoord.z * (far - near), 0.0001);

        // Use view-space depth difference scaled by view angle to approximate
        // the vertical water depth (how deep the terrain is below the water surface)
        float viewDepthDiff = terrainLinearZ - waterLinearZ;

        // Convert to approximate world-space vertical depth using view direction
        float cosViewAngle = max(abs(dot(normalize(camera.cameraPos - fragWorldPos), vec3(0.0, 1.0, 0.0))), 0.1);
        float shoreDepth = max(viewDepthDiff * cosViewAngle, 0.0);

        // Skip shore effects for deep water or when terrain is far behind
        if (shoreDepth < pc.shoreFoamRange * 2.0) {
            // Wave-responsive modulation: foam line moves with wave displacement
            float waveModulation = fragOceanDispY * 0.5;
            float effectiveShoreRange = max(pc.shoreFoamRange + waveModulation, 0.5);

            // Base shore foam gradient
            float shoreFoam = (1.0 - smoothstep(0.0, effectiveShoreRange, shoreDepth)) * pc.shoreFoamIntensity;

            // Animated foam lines rolling toward shore
            float foamLine = smoothstep(0.4, 0.5, sin(shoreDepth * 6.0 - camera.u_Time * 1.5) * 0.5 + 0.5);
            shoreFoam = max(shoreFoam, foamLine * (1.0 - smoothstep(0.0, effectiveShoreRange * 0.7, shoreDepth)) * pc.shoreFoamIntensity);

            foam += shoreFoam;

            // Shore wave breaking: boost foam where waves are steep near shore
            if (pc.shoreBreakingStrength > 0.0) {
                float shoreProximity = 1.0 - smoothstep(0.0, effectiveShoreRange * 1.5, shoreDepth);
                float waveSteepness = 1.0 - dot(N, vec3(0.0, 1.0, 0.0));
                float breaking = shoreProximity * waveSteepness * 2.0 * pc.shoreBreakingStrength;
                foam += clamp(breaking, 0.0, 1.0);
            }
        }
    }

    foam = clamp(foam, 0.0, 1.0);
    vec3 foamColor = vec3(0.95, 0.97, 1.0);
    color = mix(color, foamColor, foam * 0.6);

    float alpha = mix(pc.shallowColor.a, 1.0, fresnel);

    outColor = vec4(color, alpha);
}
