#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

// Set 0: Camera (matches CameraUBO in MeshTypes.hpp, 240 bytes)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// Set 1: Per-tile SSBO
struct WaterTileData {
    vec4 worldOriginAndSize;   // xyz = origin, w = tileSize
    vec4 heightAndWave;        // x = height, y = waveIntensity
};
layout(set = 1, binding = 0) readonly buffer TileBuffer {
    WaterTileData tiles[];
} tileData;

// Push constants (global water settings)
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
} pc;

const float PI = 3.14159265358979;

vec2 rotateDir(vec2 dir, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * dir.x - s * dir.y, s * dir.x + c * dir.y);
}

// Gerstner wave displacement
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

    // Scale unit quad to world tile
    vec3 worldPos = vec3(
        tileOrigin.x + inPosition.x * tileSize,
        waterHeight,
        tileOrigin.z + inPosition.z * tileSize
    );

    // Apply Gerstner waves (3 overlapping waves) with user-controlled direction
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

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float u_Time;
} camera;

// IBL cubemaps (same bindings as mesh.glsl)
layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// Set 2: DuDv distortion texture
layout(set = 2, binding = 0) uniform sampler2D dudvMap;

// Shared lighting includes
#include "../common/lighting_functions.glsl"
#include "../common/shadow_sampling.glsl"
#include "../common/cluster_culling.glsl"

// Set 3: Light buffers
layout(std430, set = 3, binding = 0) readonly buffer DirectionalLightBuffer { DirectionalLight directionalLights[]; };
layout(std430, set = 3, binding = 1) readonly buffer PointLightBuffer { PointLight pointLights[]; };
layout(std430, set = 3, binding = 2) readonly buffer SpotLightBuffer { SpotLight spotLights[]; };
layout(std140, set = 3, binding = 3) uniform LightCountsUBO { LightCounts lightCounts; };

// Set 4: Cluster grid params
layout(std140, set = 4, binding = 0) uniform ClusterParamsUBO { ClusterGridParams clusterParams; };

// Set 5: Cluster culling output
layout(std430, set = 5, binding = 0) readonly buffer ClusterLightGridBuffer { ClusterLightData clusterLightGrid[]; };
layout(std430, set = 5, binding = 1) readonly buffer ClusterLightIndexListBuffer { uint lightIndexList[]; };

// Set 6: Shadow data
layout(std430, set = 6, binding = 0) readonly buffer ShadowDataBuffer { ShadowData shadowDataArray[]; };

// Set 7: Shadow textures
layout(set = 7, binding = 0) uniform sampler2DShadow shadowAtlas;
layout(set = 7, binding = 1) uniform sampler2DArrayShadow shadowCascades;
layout(set = 7, binding = 2) uniform samplerCubeShadow shadowCubes[];

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
} pc;

//-----------------------------------------------------------------------------
// Shadow Constants (must match ShadowTypes.hpp)
//-----------------------------------------------------------------------------
const int MAX_SHADOW_VIEWS = 272;
const int MAX_POINT_SHADOW_CUBES = 32;

float sampleSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowDataArray[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    projCoords.xy = sd.atlasViewport.xy + projCoords.xy * sd.atlasViewport.zw;

    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 1.0;
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0)))) return 1.0;

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    float shadow = 0.0;
    float texelSize = sd.biasParams.w;
    float softness = sd.pcfParams.y;
    float spread = texelSize * softness;
    int sampleCount = 0;
    int size = kernelSize + 1;
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            vec2 offset = (vec2(float(x), float(y)) - halfSize + 0.5) * spread;
            shadow += texture(shadowAtlas, vec3(projCoords.xy + offset, projCoords.z));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

float sampleCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowDataArray[shadowIndex];

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec4 lightSpacePos = sd.viewProjection * vec4(biasedPos, 1.0);

    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    texCoords = clamp(texCoords, 0.0, 1.0);
    projCoords.xy = sd.atlasViewport.xy + texCoords * sd.atlasViewport.zw;
    projCoords.z = clamp(projCoords.z, 0.0, 1.0);

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    float shadow = 0.0;
    float spread = sd.biasParams.w * sd.pcfParams.y;
    int sampleCount = 0;
    int size = kernelSize + 1;
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            vec2 offset = (vec2(float(x), float(y)) - halfSize + 0.5) * spread;
            shadow += texture(shadowAtlas, vec3(projCoords.xy + offset, projCoords.z));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

float sampleDirectionalShadow(int baseShadowIndex, vec3 worldPos, vec3 worldNormal, float viewZ) {
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int cascadeCount = int(shadowDataArray[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);

    if (baseShadowIndex + cascadeCount > MAX_SHADOW_VIEWS) {
        cascadeCount = MAX_SHADOW_VIEWS - baseShadowIndex;
        if (cascadeCount <= 0) return 1.0;
    }

    int cascadeIdx = 0;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewZ < shadowDataArray[baseShadowIndex + i].rangeParams.y) {
            cascadeIdx = i;
            break;
        }
        cascadeIdx = i;
    }

    int shadowIndex = baseShadowIndex + cascadeIdx;
    float cascadeFar = shadowDataArray[shadowIndex].rangeParams.y;

    float shadow = sampleCascadeShadow(shadowIndex, worldPos, worldNormal);

    float blendZoneStart = cascadeFar * 0.9;
    if (viewZ > blendZoneStart && cascadeIdx < cascadeCount - 1) {
        float nextShadow = sampleCascadeShadow(shadowIndex + 1, worldPos, worldNormal);
        float blendFactor = smoothstep(blendZoneStart, cascadeFar, viewZ);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    float maxDistance = shadowDataArray[baseShadowIndex + cascadeCount - 1].rangeParams.y;
    float fadeStart = maxDistance * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxDistance, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

float samplePointShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal,
                        vec3 lightPos, float lightRadius) {
    if (shadowIndex < 0 || shadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    ShadowData sd = shadowDataArray[shadowIndex];

    int cubeMapIndex = int(sd.pcfParams.w);
    if (cubeMapIndex < 0 || cubeMapIndex >= MAX_POINT_SHADOW_CUBES) return 1.0;

    vec3 biasedPos = worldPos + worldNormal * sd.biasParams.z;
    vec3 lightToFrag = biasedPos - lightPos;
    float linearDepth = length(lightToFrag);
    vec3 sampleDir = normalize(lightToFrag);

    float near = sd.rangeParams.x;
    float far = sd.rangeParams.y;

    float majorComponent = max(abs(sampleDir.x), max(abs(sampleDir.y), abs(sampleDir.z)));
    float viewSpaceZ = linearDepth * majorComponent;
    float perspectiveDepth = (far * (viewSpaceZ - near)) / (viewSpaceZ * (far - near));

    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);

    if (!filterEnabled || kernelSize == 0) {
        return texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(sampleDir, perspectiveDepth));
    }

    float softness = sd.pcfParams.y;
    float spread = softness * 0.01;

    vec3 tangent = abs(sampleDir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 bitangent = normalize(cross(sampleDir, tangent));
    tangent = normalize(cross(bitangent, sampleDir));

    float shadow = 0.0;
    int sampleCount = 0;
    int size = kernelSize + 1;
    float halfSize = float(size) * 0.5;

    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            float fx = float(x) - halfSize + 0.5;
            float fy = float(y) - halfSize + 0.5;
            vec3 offset = tangent * fx * spread + bitangent * fy * spread;
            vec3 offsetDir = normalize(sampleDir + offset);
            shadow += texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(offsetDir, perspectiveDepth));
            sampleCount++;
        }
    }
    return shadow / float(sampleCount);
}

void main() {
    vec3 N = normalize(fragNormal);

    // Animated dudv sampling (two layers scrolling in different directions)
    float moveSpeed = pc.waveSpeed * 0.03;
    vec2 dudvUV1 = fragTexCoord * pc.dudvTiling + vec2(camera.u_Time * moveSpeed);
    vec2 dudvUV2 = fragTexCoord * pc.dudvTiling * 0.8 + vec2(-camera.u_Time * moveSpeed * 0.7, camera.u_Time * moveSpeed * 0.5);

    vec2 distortion1 = texture(dudvMap, dudvUV1).rg * 2.0 - 1.0;
    vec2 distortion2 = texture(dudvMap, dudvUV2).rg * 2.0 - 1.0;
    vec2 totalDistortion = (distortion1 + distortion2) * pc.dudvStrength;

    // Perturb normal with dudv distortion
    N = normalize(N + vec3(totalDistortion.x, 0.0, totalDistortion.y));

    vec3 V = normalize(camera.cameraPos - fragWorldPos);
    vec3 R = reflect(-V, N);

    // Fresnel (Schlick approximation)
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, pc.fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);

    // Depth-based color blend: view angle scaled by maxVisibleDepth
    float depthFactor = clamp((1.0 - NdotV) * pc.maxVisibleDepth, 0.0, 1.0);
    vec3 waterColor = mix(pc.shallowColor.rgb, pc.deepColor.rgb, depthFactor);

    // IBL reflection
    float roughness = 0.05; // Water is highly reflective
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    vec3 F0 = vec3(0.02); // Water IOR ~1.33
    vec3 specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    // Water PBR params for direct lighting
    float metallic = 0.0;
    float directRoughness = 0.3; // Higher roughness for direct lights to spread sun specular
    vec3 albedo = waterColor;

    // --- Forward+ Direct Lighting ---
    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    float linearZ = linearizeDepth(clusterParams, gl_FragCoord.z);
    uint clusterIdx = getClusterIndex(clusterParams, gl_FragCoord.xy, linearZ);

    // Cluster-culled point and spot lights
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

    // Directional lights
    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];

        float shadow = 1.0;
        if (light.shadowIndex >= 0) {
            shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, N, linearZ);
        }
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, directRoughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    // Apply shadow intensity to ambient (IBL)
    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);

    // Blend water color with reflection via Fresnel, modulated by shadow
    vec3 color = mix(waterColor, specular, fresnel) * ambientShadowFactor + directLighting;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    // Alpha from shallow color alpha, modulated by Fresnel
    float alpha = mix(pc.shallowColor.a, 1.0, fresnel);

    outColor = vec4(color, alpha);
}
