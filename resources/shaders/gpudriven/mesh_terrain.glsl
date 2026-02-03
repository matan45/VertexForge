#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;
const uint MAX_MESHLETS_PER_PAYLOAD = 512; // Must match task shader!

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragTileIndex[];
layout(location = 4) flat out uint fragMeshletIndex[];
layout(location = 5) flat out uint fragLODLevel[];
layout(location = 6) out vec2 fragWorldUV[];  // For terrain texture tiling

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Set 11 - terrain-specific data
layout(std430, set = 11, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 3, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 3, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 4, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

// Payload from task shader
struct TerrainMeshletPayload {
    uint tileIndex;
    uint lodLevel;
    uint baseVertexOffset;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT TerrainMeshletPayload payload;

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    float lodBias;
    float errorThreshold;
    float terrainTextureScale;  // Scale for world-space UV tiling
    float padding;
} pc;

// Shared memory for vertex caching
shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];
shared vec2 sharedTexCoords[MESHLET_MAX_VERTICES];

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void main() {
    uint payloadMeshletIndex = gl_WorkGroupID.x;
    if (payloadMeshletIndex >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    uint tileIndex = payload.tileIndex;
    uint lodLevel = payload.lodLevel;

    GPUMeshlet meshlet = meshlets[globalMeshletIndex];
    TerrainTileGPUData tile = tiles[tileIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    // For terrain, model matrix is usually identity, but support transforms
    mat4 modelMatrix = tile.modelMatrix;
    mat3 normalMatrix = mat3(modelMatrix);  // For orthonormal transforms
    mat4 viewProjection = camera.projection * camera.view;

    float textureScale = pc.terrainTextureScale > 0.0 ? pc.terrainTextureScale : 0.1;

    // Load vertices into shared memory
    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;
            uint baseIdx = globalVertexIndex * 16;  // 64 bytes per vertex = 16 floats

            vec3 position = vec3(
                vertexData[baseIdx + 0],
                vertexData[baseIdx + 1],
                vertexData[baseIdx + 2]
            );
            vec3 normal = vec3(
                vertexData[baseIdx + 3],
                vertexData[baseIdx + 4],
                vertexData[baseIdx + 5]
            );
            vec2 texCoord = vec2(
                vertexData[baseIdx + 6],
                vertexData[baseIdx + 7]
            );

            sharedPositions[localVertexIndex] = position;
            sharedNormals[localVertexIndex] = normal;
            sharedTexCoords[localVertexIndex] = texCoord;
        }
    }

    barrier();

    // Output vertices
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec3 localPos = sharedPositions[localVertexIndex];
            vec4 worldPos = modelMatrix * vec4(localPos, 1.0);

            fragWorldPos[localVertexIndex] = worldPos.xyz;
            fragNormal[localVertexIndex] = normalize(normalMatrix * sharedNormals[localVertexIndex]);
            fragTexCoord[localVertexIndex] = sharedTexCoords[localVertexIndex];
            fragTileIndex[localVertexIndex] = tileIndex;
            fragMeshletIndex[localVertexIndex] = globalMeshletIndex;
            fragLODLevel[localVertexIndex] = lodLevel;

            fragWorldUV[localVertexIndex] = worldPos.xz * textureScale;

            gl_MeshVerticesEXT[localVertexIndex].gl_Position = viewProjection * worldPos;
        }
    }

    // Output primitives
    uint numPrimIterations = (primitiveCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numPrimIterations; iter++) {
        uint localPrimIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localPrimIndex < primitiveCount) {
            uint packedPrimitive = meshletPrimitives[meshlet.primitiveOffset + localPrimIndex];
            uvec3 indices = unpackPrimitive(packedPrimitive);
            gl_PrimitiveTriangleIndicesEXT[localPrimIndex] = indices;
        }
    }
}

#type FRAGMENT
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/lighting_functions.glsl"
#include "../common/shadow_sampling.glsl"
#include "../common/cluster_culling.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragTileIndex;
layout(location = 4) in flat uint fragMeshletIndex;
layout(location = 5) in flat uint fragLODLevel;
layout(location = 6) in vec2 fragWorldUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    float lodBias;
    float errorThreshold;
    float terrainTextureScale;
    float padding;
} pc;

// Light buffers (Set 6 - same as mesh shader)
layout(std430, set = 6, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

layout(std430, set = 6, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

layout(std430, set = 6, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

layout(std140, set = 6, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

// Cluster grid params (Set 7)
layout(std140, set = 7, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

// Cluster culling output (Set 8)
layout(std430, set = 8, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 8, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// Shadow data (Set 9)
layout(std430, set = 9, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowDataArray[];
};

// Shadow textures (Set 10)
layout(set = 10, binding = 0) uniform sampler2DShadow shadowAtlas;
layout(set = 10, binding = 1) uniform sampler2DArrayShadow shadowCascades;
layout(set = 10, binding = 2) uniform samplerCubeShadow shadowCubes[];

//-----------------------------------------------------------------------------
// Shadow Constants (must match ShadowTypes.hpp)
//-----------------------------------------------------------------------------
const int MAX_SHADOW_VIEWS = 272;       // MAX_TOTAL_SHADOW_VIEWS
const int MAX_POINT_SHADOW_CUBES = 32;  // MAX_POINT_SHADOW_CASTERS

//-----------------------------------------------------------------------------
// Shadow Sampling Functions
//-----------------------------------------------------------------------------

float sampleSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    // Bounds validation to prevent GPU crash from invalid indices
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
    // Bounds validation to prevent GPU crash from invalid indices
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
    // Bounds validation to prevent GPU crash from invalid indices
    if (baseShadowIndex < 0 || baseShadowIndex >= MAX_SHADOW_VIEWS) return 1.0;

    int cascadeCount = int(shadowDataArray[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);

    // Ensure we don't access beyond buffer bounds with cascades
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
    // Bounds validation to prevent GPU crash from invalid indices
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
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Default terrain material properties
    // TODO: In future, these will come from terrain material system (VK-178)
    vec3 albedo = vec3(0.4, 0.35, 0.3);  // Brownish terrain color
    float metallic = 0.0;
    float roughness = 0.9;
    float ao = 1.0;

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    // IBL ambient
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao;

    // Direct lighting with shadows
    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    // Linearize depth for cluster lookup and shadow cascades
    float linearZ = linearizeDepth(clusterParams, gl_FragCoord.z);

    // Cache cluster index - used for lighting and debug visualization
    uint clusterIdx = getClusterIndex(clusterParams, gl_FragCoord.xy, linearZ);

    // Cluster-based point and spot light evaluation
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
                                                 metallic, roughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = extractLightIndex(packedIdx);
            SpotLight light = spotLights[lightIdx];

            float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
            minShadow = min(minShadow, shadow);

            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo,
                                                metallic, roughness, F0, light) * shadow;
        }
    }

    // Directional lights with CSM shadows
    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];

        // Sample directional light shadow (CSM) - only if shadow system is available
        float shadow = 1.0;
        if (light.shadowIndex >= 0) {
            shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, N, linearZ);
        }
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, roughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    // Apply shadow intensity to ambient
    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);
    ambient *= ambientShadowFactor;

    vec3 color = ambient + directLighting;

    // Tone mapping
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    // Debug view modes
    uint viewModeValue = pc.viewMode & 0xFFu;

    if (viewModeValue == 1u) {
        // Meshlet visualization
        uint h = fragMeshletIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 meshletColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        meshletColor = normalize(meshletColor + 0.1) * 0.8;
        color = meshletColor;
    }

    if (viewModeValue == 2u) {
        // LOD visualization
        vec3 lodColors[4] = vec3[4](
            vec3(0.0, 1.0, 0.0),   // LOD 0 - Green (highest detail)
            vec3(1.0, 1.0, 0.0),   // LOD 1 - Yellow
            vec3(1.0, 0.5, 0.0),   // LOD 2 - Orange
            vec3(1.0, 0.0, 0.0)    // LOD 3 - Red (lowest detail)
        );
        uint lod = min(fragLODLevel, 3u);
        color = mix(color, lodColors[lod], 0.5);
    }

    if (viewModeValue == 4u) {
        // Cluster visualization (uses cached clusterIdx)
        uint h = clusterIdx;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 clusterColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        clusterColor = normalize(clusterColor + 0.1) * 0.8;
        color = clusterColor;
    }

    if (viewModeValue == 5u) {
        // Depth slice visualization
        float near = clusterParams.depthParams.x;
        float far = clusterParams.depthParams.y;
        float normalizedDepth = clamp((linearZ - near) / (far - near), 0.0, 1.0);

        vec3 depthColors[5] = vec3[5](
            vec3(0.0, 0.0, 1.0),
            vec3(0.0, 1.0, 1.0),
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.0, 0.0)
        );
        float t = normalizedDepth * 4.0;
        int idx = clamp(int(floor(t)), 0, 3);
        color = mix(depthColors[idx], depthColors[idx + 1], fract(t));
    }

    if (viewModeValue == 6u) {
        // Shadow visualization
        float totalShadow = 1.0;

        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            float shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, N, linearZ);
            totalShadow = min(totalShadow, shadow);
        }

        if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
            // Use cached clusterIdx from earlier calculation
            ClusterLightData clusterData = clusterLightGrid[clusterIdx];
            uint clusterPointCount = getClusterPointLightCount(clusterData);
            uint clusterSpotCount = getClusterSpotLightCount(clusterData);
            uint lightOffset = clusterData.offset;

            for (uint i = 0u; i < clusterPointCount; ++i) {
                uint lightIdx = lightIndexList[lightOffset + i];
                PointLight light = pointLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N,
                                                     light.position, light.radius);
                    totalShadow = min(totalShadow, shadow);
                }
            }

            for (uint i = 0u; i < clusterSpotCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
                uint lightIdx = extractLightIndex(packedIdx);
                SpotLight light = spotLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos, N);
                    totalShadow = min(totalShadow, shadow);
                }
            }
        }

        vec3 shadowColor = mix(vec3(0.1, 0.1, 0.3), vec3(1.0, 0.95, 0.9), totalShadow);
        color = shadowColor;
    }

    if (viewModeValue == 7u) {
        // Tile visualization
        uint h = fragTileIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = ((h >> 16) ^ h) * 0x45d9f3b;
        h = (h >> 16) ^ h;

        vec3 tileColor = vec3(
            float((h >> 0) & 0xFFu) / 255.0,
            float((h >> 8) & 0xFFu) / 255.0,
            float((h >> 16) & 0xFFu) / 255.0
        );
        tileColor = normalize(tileColor + 0.1) * 0.8;
        color = tileColor;
    }

    if (viewModeValue == 8u) {
        // World UV visualization
        color = vec3(fract(fragWorldUV.x), fract(fragWorldUV.y), 0.5);
    }

    if (viewModeValue == 9u) {
        // Debug: Light count visualization
        // Red = directional count, Green = point count, Blue = spot count
        float dirCount = float(lightCounts.directionalCount) / 4.0;
        float pointCount = float(lightCounts.pointCount) / 32.0;
        float spotCount = float(lightCounts.spotCount) / 32.0;
        color = vec3(dirCount, pointCount, spotCount);
    }

    if (viewModeValue == 10u) {
        // Debug: Show direct lighting contribution only (no ambient)
        color = directLighting;
        // Boost for visibility
        color = color * 2.0;
        color = color / (color + vec3(1.0));
    }

    if (viewModeValue == 11u) {
        // Debug: Normal visualization (N * 0.5 + 0.5 to map [-1,1] to [0,1])
        color = N * 0.5 + 0.5;
    }

    if (viewModeValue == 12u) {
        // Debug: First directional light direction (if exists)
        if (lightCounts.directionalCount > 0u) {
            DirectionalLight light = directionalLights[0];
            vec3 L = -normalize(light.direction);
            float NdotL = max(dot(N, L), 0.0);
            color = vec3(NdotL);  // White = fully lit, black = no light
        } else {
            color = vec3(1.0, 0.0, 1.0);  // Magenta = no directional lights
        }
    }

    outColor = vec4(color, 1.0);
}
