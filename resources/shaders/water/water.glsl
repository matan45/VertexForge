#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) flat out uint fragFlags;
layout(location = 4) out float fragOceanDispY;  // ocean wave height displacement
layout(location = 5) out float fragBaseHeight;   // flat water height (no waves)

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

struct WaterTileData {
    vec4 worldOriginAndSize;   // xyz = origin, w = tileSize
    vec4 heightAndWave;        // x = height, y = waveIntensity
};
layout(set = 1, binding = 0) readonly buffer TileBuffer {
    WaterTileData tiles[];
} tileData;

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float waveSpeed;
    float waveAmplitude;
    float waveFrequency;
    float maxVisibleDepth;
    float fresnelPower;
    float dudvTiling;
    float dudvStrength;
    float waveDirection;
    // Ocean FFT fields
    uint oceanEnabled;
    float oceanChoppiness;
    float oceanPatchSize;
    float oceanFoamThreshold;
} pc;

layout(set = 8, binding = 0) uniform sampler2D oceanDisplacementMap;
layout(set = 8, binding = 1) uniform sampler2D oceanNormalMap;

const float PI = 3.14159265358979;

vec2 rotateDir(vec2 dir, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * dir.x - s * dir.y, s * dir.x + c * dir.y);
}

vec3 gerstnerWave(vec2 pos, vec2 dir, float steepness, float wavelength, float speed, float time) {
    float k = 2.0 * PI / wavelength;
    float c = speed / k;
    float a = steepness / k;
    vec2 d = normalize(dir);
    float f = k * (dot(d, pos) - c * time);
    return vec3(d.x * a * cos(f), a * sin(f), d.y * a * cos(f));
}

void main() {
    uint tileIndex = gl_InstanceIndex;
    vec4 originSize = tileData.tiles[tileIndex].worldOriginAndSize;
    vec4 heightWave = tileData.tiles[tileIndex].heightAndWave;

    vec3 tileOrigin = originSize.xyz;
    float tileSize = originSize.w;
    float waterHeight = heightWave.x;
    float waveIntensity = heightWave.y;

    vec3 worldPos = vec3(
        tileOrigin.x + inPosition.x * tileSize,
        waterHeight,
        tileOrigin.z + inPosition.z * tileSize
    );

    fragOceanDispY = 0.0;
    fragBaseHeight = waterHeight;

    if (pc.oceanEnabled != 0u) {
        // Ocean FFT path: sample displacement and normal from compute output
        // Two octaves at different scales to break up visible FFT tiling
        vec2 oceanUV1 = worldPos.xz / pc.oceanPatchSize;
        vec2 oceanUV2 = worldPos.xz / (pc.oceanPatchSize * 2.731) + vec2(0.37, 0.71); // irrational scale + phase offset to avoid alignment

        vec4 disp1 = texture(oceanDisplacementMap, oceanUV1);
        vec4 disp2 = texture(oceanDisplacementMap, oceanUV2);
        vec4 disp = disp1 + disp2 * 0.3; // second octave at 30% strength

        worldPos.x += disp.x;
        worldPos.y += disp.y;
        worldPos.z += disp.z;

        fragOceanDispY = disp.y;

        vec3 norm1 = texture(oceanNormalMap, oceanUV1).xyz;
        vec3 norm2 = texture(oceanNormalMap, oceanUV2).xyz;
        vec3 blendedNorm = normalize(norm1 + (norm2 - vec3(0.0, 1.0, 0.0)) * 0.3);
        fragNormal = normalize(blendedNorm);
    } else {
        // Gerstner wave path (original)
        float time = camera.u_Time * pc.waveSpeed;
        float amp = pc.waveAmplitude * waveIntensity;

        vec2 dir1 = rotateDir(vec2(1.0, 0.3), pc.waveDirection);
        vec2 dir2 = rotateDir(vec2(-0.4, 1.0), pc.waveDirection);
        vec2 dir3 = rotateDir(vec2(0.6, -0.8), pc.waveDirection);

        float freq = pc.waveFrequency;
        vec3 wave1 = gerstnerWave(worldPos.xz, dir1, 0.25 * amp, 8.0 / freq, 2.0, time);
        vec3 wave2 = gerstnerWave(worldPos.xz, dir2, 0.15 * amp, 5.0 / freq, 1.5, time);
        vec3 wave3 = gerstnerWave(worldPos.xz, dir3, 0.1 * amp, 12.0 / freq, 3.0, time);

        worldPos += wave1 + wave2 + wave3;

        // Compute normal via finite differences
        float eps = 0.1;
        vec2 baseXZ = worldPos.xz;

        vec3 posX = vec3(worldPos.x + eps, waterHeight, worldPos.z);
        posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir1, 0.25 * amp, 8.0 / freq, 2.0, time);
        posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir2, 0.15 * amp, 5.0 / freq, 1.5, time);
        posX += gerstnerWave(baseXZ + vec2(eps, 0.0), dir3, 0.1 * amp, 12.0 / freq, 3.0, time);

        vec3 posZ = vec3(worldPos.x, waterHeight, worldPos.z + eps);
        posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir1, 0.25 * amp, 8.0 / freq, 2.0, time);
        posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir2, 0.15 * amp, 5.0 / freq, 1.5, time);
        posZ += gerstnerWave(baseXZ + vec2(0.0, eps), dir3, 0.1 * amp, 12.0 / freq, 3.0, time);

        vec3 tangentX = posX - worldPos;
        vec3 tangentZ = posZ - worldPos;
        fragNormal = normalize(cross(tangentZ, tangentX));
    }

    fragWorldPos = worldPos;
    fragTexCoord = inTexCoord;
    fragFlags = floatBitsToUint(heightWave.z);

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) flat in uint fragFlags;
layout(location = 4) in float fragOceanDispY;
layout(location = 5) in float fragBaseHeight;

layout(location = 0) out vec4 outColor;

const uint WATER_FLAG_SELECTED = 1u;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(set = 2, binding = 0) uniform sampler2D dudvMap;

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
layout(set = 7, binding = 2) uniform samplerCubeShadow shadowCubes[32];
layout(set = 7, binding = 3) uniform samplerCube shadowCubesDepth[32];

#define SHADOW_BUFFER shadowDataArray
#define PAGE_TABLE pageTableData
#include "../common/shadow_sampling.glsl"

layout(set = 8, binding = 0) uniform sampler2D frag_oceanDisplacementMap;
layout(set = 8, binding = 1) uniform sampler2D frag_oceanNormalMap; // bound for descriptor set compatibility

layout(push_constant) uniform PushConstants {
    vec4 shallowColor;
    vec4 deepColor;
    float waveSpeed;
    float waveAmplitude;
    float waveFrequency;
    float maxVisibleDepth;
    float fresnelPower;
    float dudvTiling;
    float dudvStrength;
    float waveDirection;
    uint oceanEnabled;
    float oceanChoppiness;
    float oceanPatchSize;
    float oceanFoamThreshold;
} pc;

void main() {
    vec3 N = normalize(fragNormal);

    // DuDv normal distortion — skip for ocean FFT (it has its own normal map)
    if (pc.oceanEnabled == 0u) {
        float moveSpeed = pc.waveSpeed * 0.03;
        vec2 dudvUV1 = fragTexCoord * pc.dudvTiling + vec2(camera.u_Time * moveSpeed);
        vec2 dudvUV2 = fragTexCoord * pc.dudvTiling * 0.8 + vec2(-camera.u_Time * moveSpeed * 0.7, camera.u_Time * moveSpeed * 0.5);

        vec2 distortion1 = texture(dudvMap, dudvUV1).rg * 2.0 - 1.0;
        vec2 distortion2 = texture(dudvMap, dudvUV2).rg * 2.0 - 1.0;
        vec2 totalDistortion = (distortion1 + distortion2) * pc.dudvStrength;

        N = normalize(N + vec3(totalDistortion.x, 0.0, totalDistortion.y));
    }

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

    vec3 waterColor;
    float sss = 0.0;

    if (pc.oceanEnabled != 0u) {
        // Height-based color: wave peaks get shallow color, troughs get deep color
        float heightRange = fragOceanDispY;
        float heightFactor = clamp(heightRange * 0.5 + 0.5, 0.0, 1.0);
        waterColor = mix(pc.deepColor.rgb, pc.shallowColor.rgb, heightFactor);

        // Subsurface scattering approximation
        // Light passes through thin wave peaks — bright teal glow when backlit
        vec3 sssColor = vec3(0.0, 0.7, 0.6); // turquoise SSS tint
        // Use directional light if available, otherwise default sun from above
        vec3 L = normalize(vec3(0.5, 0.7, 0.3));
        vec3 lightColor = vec3(1.0);
        if (lightCounts.directionalCount > 0u) {
            L = normalize(-directionalLights[0].direction);
            lightColor = directionalLights[0].color;
        }
        // SSS strongest when looking toward the light through a wave peak
        float LdotV = max(dot(L, -V), 0.0);
        float waveHeight = clamp(heightRange, 0.0, 1.0);
        float NdotL = dot(N, L);
        float wrapDiffuse = max(0.0, (NdotL + 0.5) / 1.5);
        sss = pow(LdotV, 4.0) * waveHeight * 0.8 + wrapDiffuse * waveHeight * 0.3;
        sss = clamp(sss, 0.0, 1.0);
        waterColor = mix(waterColor, sssColor * lightColor, sss);

        // Fresnel blend — balanced for ocean (less reflection at steep angles)
        fresnel = clamp(fresnel, 0.02, 0.6);
    } else {
        float depthFactor = clamp((1.0 - NdotV) * pc.maxVisibleDepth, 0.0, 1.0);
        waterColor = mix(pc.shallowColor.rgb, pc.deepColor.rgb, depthFactor);
    }

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
            shadow = sampleDirectionalShadowAuto(light.shadowIndex, light.shadowMode, fragWorldPos, N, linearZ);
        }
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, directRoughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);

    vec3 color = mix(waterColor, specular, fresnel) * ambientShadowFactor + directLighting;

    // Ocean foam blending — only appears on large wave crests
    if (pc.oceanEnabled != 0u) {
        vec2 foamUV1 = fragWorldPos.xz / pc.oceanPatchSize;
        vec2 foamUV2 = fragWorldPos.xz / (pc.oceanPatchSize * 2.731) + vec2(0.37, 0.71);
        float foam = texture(frag_oceanDisplacementMap, foamUV1).w + texture(frag_oceanDisplacementMap, foamUV2).w * 0.3;
        vec3 foamColor = vec3(0.95, 0.97, 1.0);
        color = mix(color, foamColor, foam * 0.6);
    }

    // Tonemapping and gamma handled by post-process pipeline

    if ((fragFlags & WATER_FLAG_SELECTED) != 0u)
    {
        color = mix(color, vec3(1.0, 0.6, 0.0), 0.3);
    }

    float alpha = mix(pc.shallowColor.a, 1.0, fresnel);

    outColor = vec4(color, alpha);
}
