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
layout(location = 6) out vec2 fragWorldUV[];

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

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
    float terrainTextureScale;
    float padding;
    // Brush overlay (world space)
    float brushWorldX;
    float brushWorldZ;
    float brushWorldRadius;
    float brushFalloff;
    float brushShape;
    float _shadowLODRemoved;
    uint hiZMipLevels;           // Mip levels in the Hi-Z pyramid (0 = disabled)
    float _pad3;                 // Align mat4 to 16-byte boundary
    mat4 viewProjection;         // CPU-precomputed view-projection (matches raycast invViewProjection)
} pc;

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

    mat4 modelMatrix = tile.modelMatrix;
    mat3 normalMatrix = mat3(modelMatrix);  // For orthonormal transforms
    mat4 viewProjection = pc.viewProjection;

    float textureScale = pc.terrainTextureScale > 0.0 ? pc.terrainTextureScale : 0.1;

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
#include "../common/shadow_sampling_types.glsl"
#include "../common/cluster_culling.glsl"
#include "../common/gi_sampling.glsl"

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

layout(std430, set = 11, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 1, binding = 0) readonly buffer WeightMapBuffer {
    uint weightMapData[];
};

float readWeightByte(uint byteOffset) {
    uint wordIndex = byteOffset / 4u;
    uint byteIndex = byteOffset % 4u;
    uint word = weightMapData[wordIndex];
    return float((word >> (byteIndex * 8u)) & 0xFFu) / 255.0;
}

float sampleWeightTexel(uint tileOffset, uint res, uint channel, uint x, uint z) {
    return readWeightByte(tileOffset + (z * res + x) * 8u + channel);
}

float sampleTileWeight(uint tileOffset, uint res, uint channel, vec2 uv) {
    if (res == 0u) return (channel == 0u) ? 1.0 : 0.0;
    uv = clamp(uv, 0.0, 1.0);
    float fx = uv.x * float(res - 1u);
    float fz = uv.y * float(res - 1u);
    uint x0 = uint(floor(fx));
    uint z0 = uint(floor(fz));
    uint x1 = min(x0 + 1u, res - 1u);
    uint z1 = min(z0 + 1u, res - 1u);
    float sx = fract(fx);
    float sz = fract(fz);
    float w00 = sampleWeightTexel(tileOffset, res, channel, x0, z0);
    float w10 = sampleWeightTexel(tileOffset, res, channel, x1, z0);
    float w01 = sampleWeightTexel(tileOffset, res, channel, x0, z1);
    float w11 = sampleWeightTexel(tileOffset, res, channel, x1, z1);
    return mix(mix(w00, w10, sx), mix(w01, w11, sx), sz);
}

layout(std430, set = 1, binding = 1) readonly buffer TerrainLayerBuffer {
    TerrainLayerGPUData terrainLayers[];
};

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

// ---- SVT (Sparse Virtual Texturing) descriptors ----
#ifdef SVT_ENABLED
#include "../common/svt_types.glsl"
layout(std430, set = 12, binding = 0) readonly buffer SVTPageTableBuf {
    uint svtPageTableData[];
};
layout(set = 12, binding = 2) uniform SVTParamsUBO {
    SVTParams svtParams;
};
layout(set = 12, binding = 3) uniform sampler2DArray svtAlbedoCache;
layout(set = 12, binding = 4) uniform sampler2DArray svtNormalCache;
layout(set = 12, binding = 5) uniform sampler2DArray svtORMCache;
layout(set = 12, binding = 6) uniform sampler2DArray svtEmissionCache;
layout(set = 12, binding = 7) uniform sampler2DArray svtHeightCache;

#define SVT_PAGE_TABLE_DATA svtPageTableData
#define SVT_PARAMS svtParams
#include "../common/svt_sampling.glsl"
#endif

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    float lodBias;
    float errorThreshold;
    float terrainTextureScale;
    float padding;
    // Brush overlay (world space)
    float brushWorldX;
    float brushWorldZ;
    float brushWorldRadius;
    float brushFalloff;
    float brushShape;
    float _shadowLODRemoved;
    uint hiZMipLevels;           // Mip levels in the Hi-Z pyramid (0 = disabled)
    float _pad3;                 // Align mat4 to 16-byte boundary
    mat4 viewProjection;         // CPU-precomputed view-projection (matches raycast invViewProjection)
} pc;

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

layout(std140, set = 7, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

layout(std430, set = 8, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 8, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

layout(std430, set = 9, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowDataArray[];
};

layout(std430, set = 9, binding = 1) readonly buffer PageTableBuffer {
    uint pageTableTerrain[];
};

// Comparison samplers
layout(set = 10, binding = 0) uniform sampler2DShadow physicalPoolShadow;
layout(set = 10, binding = 1) uniform sampler2D physicalPoolDepth;

// Terrain needs higher normal bias than regular meshes to avoid self-shadow artifacts
float getTerrainNormalBiasScale() {
    return 3.0;
}

#define SHADOW_BUFFER shadowDataArray
#define PAGE_TABLE pageTableTerrain
#include "../common/shadow_sampling.glsl"

// Terrain-specific shadow wrappers that apply terrain bias scaling
float sampleTerrainSpotShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    // Apply terrain-specific normal bias scaling before delegating to VSM sampling
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return sampleVSMShadow(shadowIndex, worldPos, biasedNormal);
}

float sampleTerrainCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return sampleVSMShadow(shadowIndex, worldPos, biasedNormal);
}

float sampleTerrainDirectionalShadow(int baseShadowIndex, int shadowMode, vec3 worldPos, vec3 worldNormal, float viewZ, vec3 cameraPos) {
    // Apply terrain-specific normal bias scaling before delegating to VSM sampling
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return sampleDirectionalShadowAuto(baseShadowIndex, shadowMode, worldPos, biasedNormal, viewZ, cameraPos);
}

float sampleTerrainPointShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal,
                               vec3 lightPos, float lightRadius) {
    // Apply terrain-specific normal bias scaling before delegating to VSM sampling
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return samplePointShadow(shadowIndex, worldPos, biasedNormal, lightPos, lightRadius);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Standard layer blending (always computed — used as fallback when SVT is off or tiles missing)
#include "../material/terrain_material_generated.glsl"
#ifndef MAT_EMISSION_DEFINED
    vec3 mat_emission = vec3(0.0);
#endif

#ifdef SVT_ENABLED
    // SVT override: replace material properties with virtual texture lookup
    float svt_resolvedMip = 0.0;
    bool svt_isResident = false;

    // Bit 16 of viewMode = SVT enabled at runtime
    if ((pc.viewMode & 0x10000u) != 0u) {
        SVTSampleResult svtResult = sampleSVTFromWorld(fragWorldPos);
        svt_resolvedMip = svtResult.mipLevel;
        svt_isResident = svtResult.isResident;

        if (svtResult.isResident) {
            mat_albedo = svtResult.albedo.rgb;
            mat_normalTS = svtResult.normal;
            mat_metallic = svtResult.orm.b;
            mat_roughness = svtResult.orm.g;
            mat_ao = svtResult.orm.r;
            mat_emission = vec3(0.0);
        }
        // If not resident, keep the standard layer-blended values as fallback
    }
#endif
    vec3 albedo = mat_albedo;
    float metallic = mat_metallic;
    float roughness = mat_roughness;
    float ao = mat_ao;

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

    vec3 specularScale;
    vec3 kD;
    multiScatterCompensation(F0, brdf, metallic, specularScale, kD);

    vec3 diffuse = irradiance * albedo;
    vec3 specular = prefilteredColor * specularScale * 0.5;  // VK-1019

    float so = specularOcclusion(NdotV, ao, roughness);
    vec3 ambient = kD * diffuse * ao + specular * so;

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

            float shadow = sampleTerrainPointShadow(light.shadowIndex, fragWorldPos, N,
                                                    light.position, light.radius);

            // Weight shadow contribution to ambient by attenuation
            // so edge-of-radius precision artifacts don't darken ambient
            float dist = length(light.position - fragWorldPos);
            float atten = physicalAttenuation(dist, light.radius);
            float weightedShadow = mix(1.0, shadow, clamp(atten * 10.0, 0.0, 1.0));
            minShadow = min(minShadow, weightedShadow);

            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo,
                                                 metallic, roughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = extractLightIndex(packedIdx);
            SpotLight light = spotLights[lightIdx];

            float shadow = sampleTerrainSpotShadow(light.shadowIndex, fragWorldPos, N);

            float spotDist = length(light.position - fragWorldPos);
            float spotAtten = physicalAttenuation(spotDist, light.range);
            float weightedSpotShadow = mix(1.0, shadow, clamp(spotAtten * 10.0, 0.0, 1.0));
            minShadow = min(minShadow, weightedSpotShadow);

            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo,
                                                metallic, roughness, F0, light) * shadow;
        }
    }

    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];

        float shadow = 1.0;
        if (light.shadowIndex >= 0) {
            shadow = sampleTerrainDirectionalShadow(light.shadowIndex, light.shadowMode, fragWorldPos, N, linearZ, camera.cameraPos);
        }
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, roughness, F0, light);
        directLighting += lightContrib * shadow;
    }

    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);
    ambient *= ambientShadowFactor;

    vec3 giContribution = vec3(0.0);
#ifdef GI_ENABLED
    float cameraDist = length(camera.cameraPosition.xyz - fragWorldPos);
    vec3 giIrradiance = sampleProbeGI(fragWorldPos, N, cameraDist);
    giContribution = giIrradiance * albedo * kD;
    // Reduce ambient proportionally to GI strength to avoid double-counting
    float giStrength = min(length(giIrradiance), 1.0);
    ambient *= mix(1.0, 0.3, giStrength);
#endif

    vec3 color = ambient + directLighting + giContribution + mat_emission;

    uint viewModeValue = pc.viewMode & 0xFFu;

    if (viewModeValue == 1u) {
        uint h = fragMeshletIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
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
        vec3 lodColors[6] = vec3[6](
            vec3(0.0, 1.0, 0.0),   // LOD0: green
            vec3(1.0, 1.0, 0.0),   // LOD1: yellow
            vec3(1.0, 0.5, 0.0),   // LOD2: orange
            vec3(1.0, 0.0, 0.0),   // LOD3: red
            vec3(0.5, 0.0, 0.5),   // LOD4: purple
            vec3(0.0, 0.0, 1.0)    // LOD5: blue
        );
        uint lod = min(fragLODLevel, 5u);
        color = mix(color, lodColors[lod], 0.5);
    }

    if (viewModeValue == 3u) {
        vec2 uvDx = dFdx(fragWorldUV);
        vec2 uvDy = dFdy(fragWorldUV);
        float dx = max(length(uvDx), length(uvDy));
        float mipLevel = log2(max(dx * 1024.0, 1.0));
        mipLevel = clamp(mipLevel, 0.0, 10.0);
        vec3 mipColors[5] = vec3[5](
            vec3(0.0, 0.0, 1.0),
            vec3(0.0, 1.0, 1.0),
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.0, 0.0)
        );
        float t = mipLevel / 2.0;
        int idx = clamp(int(floor(t)), 0, 3);
        color = mix(mipColors[idx], mipColors[idx + 1], fract(t));
    }

    if (viewModeValue == 4u) {
        uint h = clusterIdx;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
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
        vec3 shadowColor = mix(vec3(0.1, 0.1, 0.3), vec3(1.0, 0.95, 0.9), minShadow);
        color = shadowColor;
    }

    if (viewModeValue == 7u) {
        uint h = fragTileIndex;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
        h = ((h >> 16) ^ h) * 0x45d9f3bu;
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
        color = vec3(fract(fragWorldUV.x), fract(fragWorldUV.y), 0.0);
    }

    if (viewModeValue == 9u) {
        vec3 layerColors[32] = vec3[32](
            vec3(0.20, 0.55, 0.20),  // Layer 0: green (grass)
            vec3(0.55, 0.40, 0.20),  // Layer 1: brown (dirt)
            vec3(0.50, 0.50, 0.50),  // Layer 2: gray (rock)
            vec3(0.85, 0.80, 0.65),  // Layer 3: sand
            vec3(0.70, 0.15, 0.15),  // Layer 4: red
            vec3(0.15, 0.30, 0.70),  // Layer 5: blue
            vec3(0.80, 0.75, 0.20),  // Layer 6: yellow
            vec3(0.55, 0.20, 0.60),  // Layer 7: purple
            vec3(0.90, 0.45, 0.10),  // Layer 8: orange
            vec3(0.10, 0.70, 0.70),  // Layer 9: teal
            vec3(0.75, 0.75, 0.75),  // Layer 10: light gray
            vec3(0.30, 0.15, 0.05),  // Layer 11: dark brown
            vec3(0.90, 0.20, 0.50),  // Layer 12: pink
            vec3(0.15, 0.55, 0.15),  // Layer 13: dark green
            vec3(0.40, 0.40, 0.80),  // Layer 14: lavender
            vec3(0.60, 0.60, 0.30),  // Layer 15: olive
            vec3(0.95, 0.90, 0.80),  // Layer 16: cream
            vec3(0.10, 0.10, 0.35),  // Layer 17: navy
            vec3(0.75, 0.35, 0.35),  // Layer 18: salmon
            vec3(0.35, 0.65, 0.45),  // Layer 19: sea green
            vec3(0.65, 0.50, 0.70),  // Layer 20: mauve
            vec3(0.85, 0.65, 0.30),  // Layer 21: gold
            vec3(0.25, 0.45, 0.25),  // Layer 22: forest
            vec3(0.70, 0.70, 0.90),  // Layer 23: periwinkle
            vec3(0.45, 0.25, 0.10),  // Layer 24: sienna
            vec3(0.20, 0.60, 0.80),  // Layer 25: sky blue
            vec3(0.80, 0.40, 0.60),  // Layer 26: rose
            vec3(0.40, 0.70, 0.30),  // Layer 27: lime
            vec3(0.60, 0.30, 0.10),  // Layer 28: rust
            vec3(0.30, 0.30, 0.30),  // Layer 29: charcoal
            vec3(0.90, 0.85, 0.40),  // Layer 30: khaki
            vec3(0.50, 0.10, 0.40)   // Layer 31: plum
        );
        uint wmOff = tiles[fragTileIndex].weightMapOffset;
        uint wmRes = uint(tiles[fragTileIndex].aabbMin.w);
        uint packedLI_vis = floatBitsToUint(tiles[fragTileIndex].aabbMax.w);
        vec3 c = vec3(0.0);
        // Must match WEIGHT_CHANNELS (terrain/TerrainWeightMap.hpp) — 4 channels, 8 bits each packed into aabbMax.w
        for (uint ch = 0u; ch < 4u; ++ch) {
            uint paletteIdx = (packedLI_vis >> (ch * 8u)) & 0xFFu;
            float w = sampleTileWeight(wmOff, wmRes, ch, fragTexCoord);
            c += layerColors[min(paletteIdx, 31u)] * w;
        }
        color = c;
    }

    // Clipmap/Cascade Level Visualization (terrain)
    if (viewModeValue == 14u) {
        color = vec3(0.2);
        DirectionalLight light = directionalLights[0];
        if (lightCounts.directionalCount > 0u && light.shadowIndex >= 0) {
            int levelCount = int(SHADOW_BUFFER[light.shadowIndex].rangeParams.z);
            float baseExtent = SHADOW_BUFFER[light.shadowIndex].rangeParams.x;
            float worldDist = length(fragWorldPos - camera.cameraPos);

            if (light.shadowMode == 1) {
                float lvl = max(log2(max(worldDist, baseExtent) / baseExtent), 0.0);
                int levelIdx = clamp(int(lvl), 0, levelCount - 1);
                float t = float(levelIdx) / max(float(levelCount - 1), 1.0);
                color = mix(vec3(0.0, 0.2, 1.0), vec3(1.0, 0.2, 0.0), t);
                if (fract(lvl) < 0.02 || fract(lvl) > 0.98) color = vec3(1.0);
            } else {
                vec3 cascadeColors[4] = vec3[](
                    vec3(1.0, 0.2, 0.2), vec3(1.0, 0.7, 0.2),
                    vec3(0.9, 0.9, 0.2), vec3(0.2, 1.0, 0.3));
                // Use world distance for cascade visualization on terrain
                int cascadeIdx = clamp(int(worldDist / 50.0), 0, min(levelCount, 4) - 1);
                color = cascadeColors[cascadeIdx];
            }
        }
    }

    // Shadow UV Visualization (terrain)
    if (viewModeValue == 15u) {
        color = vec3(0.0);
        if (lightCounts.directionalCount > 0u) {
            DirectionalLight light = directionalLights[0];
            if (light.shadowIndex >= 0) {
                ShadowData sd = SHADOW_BUFFER[light.shadowIndex];
                vec4 lsPos = sd.viewProjection * vec4(fragWorldPos, 1.0);
                vec2 uv = lsPos.xy * 0.5 + 0.5;
                bool valid;
                vec2 physUV = vsmLookupPhysicalUV(sd, uv, valid);
                color = vec3(uv.x, uv.y, valid ? 0.5 : 0.0);
            }
        }
    }

    // Tile selection highlight
    const uint FLAG_SELECTED = 1u << 13;
    if ((tiles[fragTileIndex].flags & FLAG_SELECTED) != 0u) {
        vec3 highlightColor = vec3(1.0, 1.0, 0.0);
        color = mix(color, highlightColor, 0.25);
    }

    if (pc.brushWorldRadius > 0.0) {
        vec2 brushPos = vec2(pc.brushWorldX, pc.brushWorldZ);
        vec2 delta = fragWorldPos.xz - brushPos;
        uint shapeType = uint(pc.brushShape);

        float dist;
        if (shapeType == 1u) {
            dist = max(abs(delta.x), abs(delta.y)) / pc.brushWorldRadius;
        } else {
            dist = length(delta) / pc.brushWorldRadius;
        }

        if (dist <= 1.0) {
            float falloffValue;
            uint falloffType = uint(pc.brushFalloff);

            if (falloffType == 0u) { falloffValue = 1.0; }
            else if (falloffType == 1u) { falloffValue = 1.0 - dist; }
            else if (falloffType == 2u) { falloffValue = 1.0 - dist*dist*(3.0-2.0*dist); }
            else { falloffValue = pow(1.0 - dist, 3.0); }

            vec3 brushColor = vec3(0.2, 0.6, 1.0);
            color = mix(color, brushColor, falloffValue * 0.3);
        }

        float edgeWidth = 0.02;
        float edgeDist = abs(dist - 1.0);
        if (edgeDist < edgeWidth) {
            float edgeAlpha = 1.0 - (edgeDist / edgeWidth);
            vec3 brushColor = vec3(0.2, 0.6, 1.0);
            color = mix(color, brushColor, edgeAlpha * 0.8);
        }
    }

    outColor = vec4(color, 1.0);
}
