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
layout(location = 7) flat out uint fragIsCave[];

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
    float brushWorldY;
    uint hiZMipLevels;           // Mip levels in the Hi-Z pyramid (0 = disabled)
    float _pad3;                 // Align mat4 to 16-byte boundary
    mat4 viewProjection;         // CPU-precomputed view-projection (matches raycast invViewProjection)
    // Stamp overlay
    uint stampWidth;
    uint stampHeight;
    float stampRotation;
    float _padStamp;
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

    uint packedMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    bool isCave = (packedMeshletIndex & 0x80000000u) != 0u;
    uint globalMeshletIndex = packedMeshletIndex & 0x7FFFFFFFu;
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
            fragIsCave[localVertexIndex] = isCave ? 1u : 0u;

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
#include "../common/wetness.glsl"
#include "../common/snow_accumulation.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragTileIndex;
layout(location = 4) in flat uint fragMeshletIndex;
layout(location = 5) in flat uint fragLODLevel;
layout(location = 6) in vec2 fragWorldUV;
layout(location = 7) in flat uint fragIsCave;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

// VK-1574: Y-axis rotation of an IBL sample direction (cs = vec2(cos(theta), sin(theta))).
vec3 iblRotateY(vec3 v, vec2 cs) {
    return vec3(cs.x * v.x + cs.y * v.z, v.y, -cs.y * v.x + cs.x * v.z);
}

layout(std430, set = 11, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 11, binding = 2) readonly buffer StampOverlayData {
    float stampHeights[];
};

#ifdef WORLD_MASK_ENABLED
// Plugin world-space mask (VK-1359): XZ-projected over worldMinMax bounds.
layout(set = 11, binding = 3) uniform sampler2D worldMaskTexture;
layout(set = 11, binding = 4) uniform WorldMaskUBO {
    vec4 worldMinMax;          // minX, minZ, maxX, maxZ
    float terrainDimMin;
    float entityDiscardBelow;
    uint flags;                // bit0 enabled, bit1 affectsTerrain, bit2 affectsEntities, bit3 affectsShadows
    float _padWM;
} worldMask;
#endif

#ifdef TERRAIN_WEATHER_MASK
// VK-1614 world-anchored wetness/snow mask: R = wetness, G = snow, XZ-projected over worldMinMax.
// Bindings 5/6 of set 11, mirroring the plugin world mask at 3/4 — set 11 is the right home here
// precisely because it does NOT exist in the RVT bake pipeline (TerrainRVTBaker binds
// {weightMap, bindless, terrainData} as its sets 0/1/2), and the mask must not reach the bake.
// That is the inverse of VK-1611's anti-tiling block, which had to go on the weight-map set for
// exactly the opposite reason.
layout(set = 11, binding = 5) uniform sampler2D terrainWeatherMaskTexture;
layout(set = 11, binding = 6) uniform TerrainWeatherMaskUBO {
    vec4 worldMinMax;          // minX, minZ, maxX, maxZ — AUTHORED, never derived from live grid bounds
    float wetnessScale;        // authored strength multiplier on the R channel
    float snowScale;           // authored strength multiplier on the G channel
    uint flags;                // bit0 enabled
    float _padTWM;
} weatherMask;
#endif

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

// VK-1611 material-global anti-tiling. Binding 2 of the weight-map set is the ONE home reachable
// from both composite consumers: terrain_rvt_bake.glsl binds this very same descriptor set as its
// set 0 (TerrainRVTBaker::init takes {weightMap, bindless, terrainData} and nothing else), so the
// generated snippet reads identical values in the live and baked paths. Set 11 — which the story
// originally called for — does not exist in the bake pipeline at all.
layout(std430, set = 1, binding = 2) readonly buffer TerrainAntiTilingBuffer {
    TerrainAntiTilingGPUData terrainAntiTiling;
};

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

// VK-1611: world-anchored value noise for macro variation. File scope, because the generated
// composite is #include-d INSIDE main() and so cannot declare functions of its own.
#include "../common/terrain_value_noise.glsl"
#ifdef TERRAIN_HEX_TILING
// VK-1612: must follow the bindlessTextures declaration — the helpers index it directly rather
// than taking a sampler2D parameter.
#include "../common/hex_tiling_terrain.glsl"
#endif

// VK-1614. TERRAIN_LOCAL_WEATHER is the union gate: it selects the unified weather block at the apply
// point, and each individual feature inside it stays behind its own macro. CAUSTICS_ENABLED is part of
// the union because this story folds the shoreline wetness — previously a second, uncoordinated
// material model — into the same signal, so a caustics build takes the unified path even with no mask
// and no authored layer.
#if defined(TERRAIN_WEATHER_RESPONSE) || defined(TERRAIN_WEATHER_MASK) || defined(TERRAIN_PUDDLES) || defined(CAUSTICS_ENABLED)
#define TERRAIN_LOCAL_WEATHER 1
// Must follow tiles[] / weightMapData[] / sampleTileWeight / terrainLayers[] — the splat gather reads
// them directly rather than taking parameters, same contract as hex_tiling_terrain.glsl above.
#include "../common/terrain_weather.glsl"
#endif

#ifdef RVT_ENABLED
// VK-1209 terrain Runtime Virtual Texture (set 5, the previously-empty placeholder set).
// Replaces the per-fragment 8-layer composite (up to 32 bindless samples with detail maps) with a lookup
// into the baked page atlas + an inline feedback request. Compiled only when RVT is active.
#include "../common/vt_types.glsl"
layout(std430, set = 5, binding = 0) readonly buffer RVTPageTable { uint rvtPageTable[]; };
layout(set = 5, binding = 1) uniform sampler2D rvtAlbedoAtlas;
layout(set = 5, binding = 2) uniform sampler2D rvtOrmAtlas;
layout(std430, set = 5, binding = 3) buffer RVTFeedback { uint rvtFeedback[]; };
layout(set = 5, binding = 4) uniform RVTParams {
    VTImageInfo img;
    vec2 worldMin;        // terrain XZ origin
    vec2 invWorldExtent;  // 1 / (worldMax - worldMin)
    float virtualResTexels;
    // VK-1620: the world-height plane's decode range. The terrain pipeline does not sample that
    // plane — these are here because the scene mesh pipeline reads THIS SAME struct (it is uploaded
    // once and consumed by both) and needs them to turn a normalized height back into a world Y.
    float heightMin; float heightRange;
    float pad2;
} rvt;
#ifdef TERRAIN_DETAIL_MAPS
layout(set = 5, binding = 5) uniform sampler2D rvtNormalAtlas;
layout(set = 5, binding = 6) uniform sampler2D rvtEmissionAtlas;
#endif
#define VT_PAGE_TABLE rvtPageTable
#define VT_FEEDBACK rvtFeedback
#include "../common/vt_sampling.glsl"
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
    float brushWorldY;
    uint hiZMipLevels;           // Mip levels in the Hi-Z pyramid (0 = disabled)
    float _pad3;                 // Align mat4 to 16-byte boundary
    mat4 viewProjection;         // CPU-precomputed view-projection (matches raycast invViewProjection)
    // Stamp overlay
    uint stampWidth;
    uint stampHeight;
    float stampRotation;
    float _padStamp;
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

// All three RT shadow masks share one set (set 13); see mesh_shader_gpudriven.glsl for the rationale.
#ifdef RT_SHADOW_ENABLED
layout(set = 13, binding = 0) uniform sampler2D rtShadowMask;                 // directional (VK-1150)
#endif

#ifdef RT_SPOT_SHADOW_ENABLED
// Per-spot-light RT shadow masks (VK-1175).
layout(set = 13, binding = 1) uniform sampler2DArray rtSpotShadowMaskArray;
#endif

#ifdef RT_POINT_SHADOW_ENABLED
// Per-point-light RT shadow masks (VK-1176).
layout(set = 13, binding = 2) uniform sampler2DArray rtPointShadowMaskArray;
#endif

#ifdef CAUSTICS_ENABLED
layout(set = CAUSTIC_SET, binding = 0) uniform sampler2D causticMap;
layout(set = CAUSTIC_SET, binding = 1) uniform CausticParamsUBO {
    float waterHeight;
    float causticStrength;
    float depthFalloff;
    float patchSize;
    float shoreWetRange;
    // VK-1614 retired shoreWetDarkening / shoreWetRoughness — the shoreline feeds the unified terrain
    // wetness signal now instead of applying a second material model of its own.
    float pad0;
    float pad1;
    float pad2;
} causticParams;
#include "../common/caustic_sampling.glsl"
#endif

// VK-1577: local reflection probes at set 0, bindings 4/5 (see mesh_shader_gpudriven.glsl).
#ifdef REFLECTION_PROBES_ENABLED
#include "../common/reflection_probes.glsl"
#endif

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

float sampleTerrainSpotShadowHybrid(int shadowIndex, int rtMaskSlice, vec3 worldPos, vec3 worldNormal) {
#ifdef RT_SPOT_SHADOW_ENABLED
    // Optional RT override (off by default): keep terrain in lockstep with meshes so a budgeted
    // spot light's RT shadow lands on both surfaces (no RT-on-mesh / VSM-on-terrain mismatch).
    if (lightCounts.rtSpotShadowActive != 0u && rtMaskSlice >= 0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtSpotShadowMaskArray, vec3(screenUV, float(rtMaskSlice))).r;
    }
#endif
    return sampleTerrainSpotShadow(shadowIndex, worldPos, worldNormal);
}

float sampleTerrainCascadeShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal) {
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return sampleVSMShadow(shadowIndex, worldPos, biasedNormal);
}

float sampleTerrainDirectionalShadow(int baseShadowIndex, int shadowMode, vec3 worldPos, vec3 worldNormal, float viewZ, vec3 cameraPos) {
#ifdef RT_SHADOW_ENABLED
    // Optional RT override (off by default).
    if (lightCounts.rtShadowActive != 0u) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtShadowMask, screenUV).r;
    }
#endif
    if (baseShadowIndex < 0) return 1.0;
    // shadowMode 1 = VSM clipmap. Apply terrain-specific normal-bias scaling first.
    if (shadowMode == 1) {
        vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
        return sampleDirectionalVSM(baseShadowIndex, worldPos, biasedNormal);
    }
    return 1.0;
}

float sampleTerrainPointShadow(int shadowIndex, vec3 worldPos, vec3 worldNormal,
                               vec3 lightPos, float lightRadius) {
    // Apply terrain-specific normal bias scaling before delegating to VSM sampling
    vec3 biasedNormal = worldNormal * getTerrainNormalBiasScale();
    return samplePointShadow(shadowIndex, worldPos, biasedNormal, lightPos, lightRadius);
}

float sampleTerrainPointShadowHybrid(int shadowIndex, int rtMaskSlice, vec3 worldPos, vec3 worldNormal,
                                     vec3 lightPos, float lightRadius) {
#ifdef RT_POINT_SHADOW_ENABLED
    // Optional RT override (off by default): keep terrain in lockstep with meshes so a budgeted
    // point light's RT shadow lands on both surfaces (no RT-on-mesh / VSM-on-terrain mismatch).
    if (lightCounts.rtPointShadowActive != 0u && rtMaskSlice >= 0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtPointShadowMaskArray, vec3(screenUV, float(rtMaskSlice))).r;
    }
#endif
    return sampleTerrainPointShadow(shadowIndex, worldPos, worldNormal, lightPos, lightRadius);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    // Triplanar UV blending for cave walls/ceilings where XZ projection stretches
    vec3 blendWeights = abs(N);
    blendWeights = pow(blendWeights, vec3(4.0));
    blendWeights /= (blendWeights.x + blendWeights.y + blendWeights.z);
    float textureScale = pc.terrainTextureScale > 0.0 ? pc.terrainTextureScale : 0.1;
    vec2 uvXZ = fragWorldPos.xz * textureScale; // Y-facing (horizontal surfaces)
    vec2 uvXY = fragWorldPos.xy * textureScale; // Z-facing (north/south walls)
    vec2 uvYZ = fragWorldPos.yz * textureScale; // X-facing (east/west walls)
    vec2 blendedTriplanarWorldUV = uvXZ * blendWeights.y + uvXY * blendWeights.z + uvYZ * blendWeights.x;
#if defined(RVT_ENABLED) && defined(TERRAIN_DETAIL_MAPS)
    // Match the bake's top-down projection for cache hits and live fallback. Caves are never
    // RVT-resolved and retain triplanar projection for their walls and ceilings.
    vec2 triplanarWorldUV = (fragIsCave == 0u) ? uvXZ : blendedTriplanarWorldUV;
#else
    vec2 triplanarWorldUV = blendedTriplanarWorldUV;
#endif
    // VK-1209 finding #7: screen-space gradients of the (quad-uniform) triplanar UV, computed here in
    // UNIFORM control flow so the generated composite can sample with textureGrad — its layer samples run
    // inside the per-fragment-divergent RVT resolved/fallback branch below, where implicit derivatives
    // are undefined and shimmer at RVT page seams.
    vec2 triplanarWorldUVdx = dFdx(triplanarWorldUV);
    vec2 triplanarWorldUVdy = dFdy(triplanarWorldUV);

    // VK-1611 composite contract. terrainWorldXZ is the UNSCALED world position: macro variation
    // is world-anchored, and triplanarWorldUV is already multiplied by textureScale and is
    // triplanar-blended inside caves, so it cannot stand in for it.
    vec2 terrainWorldXZ = fragWorldPos.xz;
    // Footprint = log2(texture repeats per output texel), the distance signal that is meaningful
    // in the camera-less RVT bake as well as here. rho_MAX with no anisotropy division: Vulkan's
    // lambda_base divides by eta and therefore follows the minor axis, which under a grazing RTS
    // view reports "near" for distant ground. Computed rather than queried because the spec only
    // BOUNDS hardware rho, so textureQueryLod is not reproducible across vendors — disqualifying
    // for content baked into shared pages.
    float terrainFootprintLog2 = 0.5 * log2(max(max(dot(triplanarWorldUVdx, triplanarWorldUVdx),
                                                    dot(triplanarWorldUVdy, triplanarWorldUVdy)), 1e-30));

#ifdef RVT_ENABLED
    // Sample the baked terrain RVT atlas (2 or 4 planes) instead of the live 8-layer composite.
    // A lookup "resolves" only when the page-table entry is valid AND the ORM atlas alpha
    // (the per-texel "baked with real content" bit, written 1.0 by terrain_rvt_bake.glsl) is
    // set. Any fragment that is not truly resident-with-content — streaming in, on an
    // uncovered page, or on a mapped-but-not-baked tile (clear leaves alpha 0) — falls back
    // to the live composite so the surface is never worse than the non-RVT path, never black.
    vec3 mat_albedo;
    float mat_metallic;
    float mat_roughness;
    float mat_ao;
    vec3 mat_emission;
#ifdef TERRAIN_DETAIL_MAPS
    vec3 mat_normalTS;
#endif
    vec2 rvtUV = clamp((fragWorldPos.xz - rvt.worldMin) * rvt.invWorldExtent, vec2(0.0), vec2(0.999999));
    uint rvtMip = uint(max(vtDesiredMip(rvtUV, rvt.virtualResTexels), 0.0));
#ifdef TERRAIN_DETAIL_MAPS
    bool rvtSurfaceEligible = fragIsCave == 0u;
#else
    bool rvtSurfaceEligible = true;
#endif
    if (rvtSurfaceEligible && vtFeedbackFragment(gl_FragCoord.xy))
        vtWriteFeedback(rvt.img, rvtUV, rvtMip);
    VTSample rvtS = vtLookup(rvt.img, rvtUV, rvtMip);
    vec4 rvtO = (rvtSurfaceEligible && rvtS.valid) ? texture(rvtOrmAtlas, rvtS.uv) : vec4(0.0);
    bool rvtResolved = rvtSurfaceEligible && rvtS.valid && rvtO.a >= 0.5;
    // VK-1611 bake/live parity for the footprint-driven distance rescale. A fragment that falls
    // back to the live composite sits right next to fragments served from a baked page, and the
    // bake computed ITS footprint from the page's texel size, not from screen derivatives. Keying
    // the fallback off the same page's mip is what makes the rescale transition continuous across
    // the page-residency boundary instead of drawing a seam that crawls with the camera — the
    // failure mode VK-1609 documented and that an RVT-off test cannot see.
    //
    // The bake's world step per fragment is exactly the page's texel size, T0 * 2^mip: it expands
    // the page rect by VT_BORDER / VT_PAGE_INTERIOR and then rasterizes the full 128-texel tile,
    // and those two cancel. T0 is the mip-0 world texel size. Selected branchlessly; this sits in
    // uniform control flow, above the divergent branch, so no derivative is taken inside it.
    float rvtWorldTexel0 = 1.0 / max(rvt.invWorldExtent.x * rvt.virtualResTexels, 1e-20);
    float rvtBakeFootprintLog2 = log2(max(rvtWorldTexel0 * textureScale, 1e-20))
                               + float(rvtS.valid ? rvtS.residentMip : rvtMip);
    terrainFootprintLog2 = rvtSurfaceEligible ? rvtBakeFootprintLog2 : terrainFootprintLog2;
    if (rvtResolved) {
        vec4 rvtA = texture(rvtAlbedoAtlas, rvtS.uv);
        // Coverage renormalization: rvtO.a is the per-texel baked-coverage bit (1.0 baked,
        // 0.0 cleared), so a bilinear tap straddling covered and cleared texels returns every
        // channel pre-scaled by the filtered coverage — rendering as a thin dark seam line at
        // bake-quad seams and page borders at the terrain edge. Dividing by the filtered
        // coverage reconstructs the covered texels' average instead. Fully covered taps have
        // a == 1.0 exactly, so the division is an exact no-op on the interior fast path.
        float rvtCov = rvtO.a;
        mat_albedo = rvtA.rgb / rvtCov;
        mat_metallic = rvtO.b / rvtCov;
        mat_roughness = rvtO.g / rvtCov;
        mat_ao = rvtO.r / rvtCov;
#ifdef TERRAIN_DETAIL_MAPS
        vec3 rvtNormalEncoded = texture(rvtNormalAtlas, rvtS.uv).rgb / rvtCov;
        vec3 rvtNormalDecoded = rvtNormalEncoded * 2.0 - 1.0;
        float rvtNormalLengthSq = dot(rvtNormalDecoded, rvtNormalDecoded);
        mat_normalTS = (rvtNormalLengthSq > 1e-8)
            ? rvtNormalDecoded * inversesqrt(rvtNormalLengthSq)
            : vec3(0.0, 0.0, 1.0);
        mat_emission = texture(rvtEmissionAtlas, rvtS.uv).rgb / rvtCov;
#else
        mat_emission = mat_albedo * (rvtA.a / rvtCov);
#endif
    } else {
        // Live 8-layer composite fallback (cold path — only unresolved fragments pay it, so the
        // RVT fast path keeps its win). The generated composite declares its own mat_* locals;
        // rename them to temporaries so they don't clash with the outer decls, then copy out.
        #define mat_albedo   _rvtcAlbedo
        #define mat_metallic _rvtcMetallic
        #define mat_roughness _rvtcRoughness
        #define mat_ao       _rvtcAO
        #define mat_emission _rvtcEmission
#ifdef TERRAIN_DETAIL_MAPS
        #define mat_normalTS _rvtcNormalTS
#endif
        #include "../material/terrain_material_generated.glsl"
        #undef mat_albedo
        #undef mat_metallic
        #undef mat_roughness
        #undef mat_ao
        #undef mat_emission
#ifdef TERRAIN_DETAIL_MAPS
        #undef mat_normalTS
#endif
        mat_albedo = _rvtcAlbedo;
        mat_metallic = _rvtcMetallic;
        mat_roughness = _rvtcRoughness;
        mat_ao = _rvtcAO;
        mat_emission = _rvtcEmission;
#ifdef TERRAIN_DETAIL_MAPS
        mat_normalTS = _rvtcNormalTS;
#endif
    }
#else
#include "../material/terrain_material_generated.glsl"
#ifndef MAT_EMISSION_DEFINED
    vec3 mat_emission = vec3(0.0);
#endif
#endif
#ifdef TERRAIN_DETAIL_MAPS
#ifndef MAT_NORMALTS_DEFINED
    vec3 mat_normalTS = vec3(0.0, 0.0, 1.0);
#endif
    // Apply the tangent-space detail once after RVT/live selection. Test the orthogonalized
    // tangent rather than its raw derivative form so degenerate projections retain N exactly.
    vec3 terrainGeometricNormal = N;
    vec3 terrainPosDx = dFdx(fragWorldPos);
    vec3 terrainPosDy = dFdy(fragWorldPos);
    vec3 terrainTangentRaw = terrainPosDx * triplanarWorldUVdy.y
                           - terrainPosDy * triplanarWorldUVdx.y;
    // terrainTangentRaw == (dPos/dU) * det(UV screen-space Jacobian), so its sign follows sign(det).
    // Re-align to +U by folding in that sign: otherwise on fragments where the top-down XZ UV winds
    // negative in screen space the tangent (and its cross(N,tangent) bitangent) flip together and
    // invert the applied tangent-space normal detail (bumps read as dents as the camera rotates).
    float terrainUVDet = triplanarWorldUVdx.x * triplanarWorldUVdy.y
                       - triplanarWorldUVdy.x * triplanarWorldUVdx.y;
    terrainTangentRaw *= (terrainUVDet < 0.0) ? -1.0 : 1.0;
    vec3 terrainTangentProjected = terrainTangentRaw - N * dot(N, terrainTangentRaw);
    float terrainTangentLengthSq = dot(terrainTangentProjected, terrainTangentProjected);
    if (terrainTangentLengthSq > 1e-12) {
        vec3 terrainTangent = terrainTangentProjected * inversesqrt(terrainTangentLengthSq);
        vec3 terrainBitangent = cross(N, terrainTangent);
        vec3 terrainMappedNormal = mat3(terrainTangent, terrainBitangent, N) * mat_normalTS;
        float terrainMappedLengthSq = dot(terrainMappedNormal, terrainMappedNormal);
        if (terrainMappedLengthSq > 1e-8)
            N = terrainMappedNormal * inversesqrt(terrainMappedLengthSq);
    }
    #define TERRAIN_SHADOW_NORMAL terrainGeometricNormal
#else
    #define TERRAIN_SHADOW_NORMAL N
#endif
    vec3 albedo = mat_albedo;
    float metallic = mat_metallic;
    float roughness = mat_roughness;
    float ao = mat_ao;

    // Distinct cave-interior look: carved cave walls keep the surface's triplanar detail
    // but read as darker, rougher, non-metallic rock so interiors don't share the exact
    // surface material. (fragIsCave is set per cave meshlet by the mesh stage.)
    if (fragIsCave != 0u) {
        albedo = mix(albedo, albedo * vec3(0.45, 0.42, 0.40), 0.75);
        roughness = clamp(max(roughness, 0.9), 0.0, 1.0);
        metallic = 0.0;
        ao = min(ao, 0.85);
    }

#ifdef CAUSTICS_ENABLED
    // Shoreline wetness, VK-1614: this block now only COMPUTES how wet the shore is. It used to
    // apply its own material model here — `albedo *= mix(1, shoreWetDarkening, w)`,
    // `roughness = mix(roughness, max(roughness, shoreWetRoughness), w)` and an AO raise — running
    // immediately before applyWetness, which pulls roughness the OTHER WAY (x0.3). Only one of those
    // can be right: wet surfaces get smoother. Worse, shoreWetRoughness defaulted to 0.15 against a
    // terrain layer roughness default of 0.9, so `max(0.9, 0.15)` made that slider a no-op across its
    // whole plausible range. Both authored knobs are retired; shoreWetRange still sets the extent.
    float shoreWet = 0.0;
    if (causticParams.shoreWetRange > 0.0) {
        float waveFreq = 6.2831853 / causticParams.patchSize;
        float waveApprox = sin(fragWorldPos.x * waveFreq + fragWorldPos.z * waveFreq * 1.5 + camera.time) * 0.5;
        float heightAboveWater = fragWorldPos.y - causticParams.waterHeight + waveApprox;
        // Fade in from the waterline up to shoreWetRange...
        shoreWet = 1.0 - smoothstep(0.0, causticParams.shoreWetRange, heightAboveWater);
        // ...and out below it (terrain underwater doesn't need the wet look).
        shoreWet *= smoothstep(-1.0, 0.0, heightAboveWater);
        shoreWet *= shoreWet;
    }
#endif

    // Weather surface effects (wetness first, then snow on top)
#ifdef TERRAIN_LOCAL_WEATHER
    // VK-1614. Deliberately HERE, after the RVT-resolve / live-composite join above, so baked pages
    // stay weather-independent and one page serves every weather state.
    float twWet = camera.wetness;
    float twSnow = camera.snowAccumulation;
#ifdef CAUSTICS_ENABLED
    twWet = terrainWeatherOr(twWet, shoreWet);
#endif
#ifdef TERRAIN_WEATHER_MASK
    if ((weatherMask.flags & 1u) != 0u) {
        vec2 twMaskUV = (fragWorldPos.xz - weatherMask.worldMinMax.xy)
                      / max(weatherMask.worldMinMax.zw - weatherMask.worldMinMax.xy, vec2(1e-6));
        // Outside the authored rect the mask contributes nothing, so terrain that streamed in beyond
        // it falls back to global-only weather instead of sampling a clamped edge texel.
        if (all(greaterThanEqual(twMaskUV, vec2(0.0))) && all(lessThanEqual(twMaskUV, vec2(1.0)))) {
            vec2 twM = texture(terrainWeatherMaskTexture, twMaskUV).rg;
            twWet = terrainWeatherOr(twWet, twM.r * weatherMask.wetnessScale);
            twSnow = terrainWeatherOr(twSnow, twM.g * weatherMask.snowScale);
        }
    }
#endif
    // The derived porosity is computed HERE, not earlier: wetness.glsl derives it from the roughness
    // AS IT ARRIVES, i.e. after the cave darkening above forces roughness >= 0.9. Hoisting it would
    // change today's output.
    float twPorosity = clamp(roughness * roughness, 0.0, 1.0);
    float twRetention = 1.0;
#ifdef TERRAIN_WEATHER_RESPONSE
    TerrainWeatherResponse twR = sampleTerrainWeatherResponse(fragTileIndex, fragTexCoord);
    // mix(x, y, 0.0) == x bitwise, and the T factors are EXACTLY 0 when no layer covering this
    // fragment opted in — so a tile painted only with unauthored layers renders bit-identically to
    // the pre-VK-1614 shader even inside this permutation.
    twPorosity = mix(twPorosity, twR.porosity, twR.porosityT);
    twRetention = mix(twRetention, twR.retention, twR.retentionT);
#endif
#ifdef TERRAIN_PUDDLES
    // Computed BEFORE applyTerrainWetness, which mutates the roughness twPorosity was derived from
    // and the albedo the puddle then tints.
    float twPuddle = terrainPuddleCoverage(twWet, normalize(fragNormal).y, ao, twPorosity);
#endif
    applyTerrainWetness(twWet, twPorosity, albedo, roughness, metallic, N);
#ifdef TERRAIN_PUDDLES
    applyTerrainPuddle(twPuddle, albedo, roughness, metallic, N);
#endif
    // Retention folds into the AMOUNT rather than needing a new parameter: applySnowAccumulation
    // computes snowFactor = amount * slopeMask, and (amount * retention) * slopeMask ==
    // snowFactor * retention. So snow_accumulation.glsl is untouched — which keeps
    // mesh_shader_gpudriven.glsl byte-identical — and its `snowAmount < 0.001` early-out makes a
    // fully-shedding layer free.
    applySnowAccumulation(twSnow * twRetention, fragNormal, albedo, roughness, metallic, N);
#else
    applyWetness(camera.wetness, albedo, roughness, metallic, N);
    applySnowAccumulation(camera.snowAccumulation, fragNormal, albedo, roughness, metallic, N);
#endif

    // Fog-of-war visibility for this terrain fragment (1.0 = fully visible). Stays 1.0 unless
    // the bound mask opts in to affecting shadows (bit3), so shadow fading is configurable.
    float worldMaskVisibility = 1.0;
#ifdef WORLD_MASK_ENABLED
    // Plugin world mask: dim albedo where the XZ-projected mask is low (e.g. fog of war), and/or
    // fade cast shadows there. Fragments outside the mask bounds are unaffected (mask = 1.0).
    if ((worldMask.flags & 1u) != 0u) {   // enabled
        bool affectsTerrain = (worldMask.flags & 2u) != 0u;
        bool affectsShadows = (worldMask.flags & 8u) != 0u;
        if (affectsTerrain || affectsShadows) {
            vec2 maskUV = (fragWorldPos.xz - worldMask.worldMinMax.xy)
                        / (worldMask.worldMinMax.zw - worldMask.worldMinMax.xy);
            if (all(greaterThanEqual(maskUV, vec2(0.0))) && all(lessThanEqual(maskUV, vec2(1.0)))) {
                float maskValue = texture(worldMaskTexture, maskUV).r;
                if (affectsTerrain) albedo *= mix(worldMask.terrainDimMin, 1.0, maskValue);
                if (affectsShadows) worldMaskVisibility = maskValue;
            }
        }
    }
#endif

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 irradiance = texture(irradianceMap, iblRotateY(N, camera.iblRotation.xy)).rgb;
    vec3 prefilteredColor = textureLod(prefilterMap, iblRotateY(R, camera.iblRotation.xy), roughness * MAX_REFLECTION_LOD).rgb;
#ifdef REFLECTION_PROBES_ENABLED
    // VK-1577: same probe override as the mesh path — terrain inside a probe (a cave floor, an
    // interior courtyard) must agree with the meshes standing on it, or the two disagree exactly at
    // the contact point.
    float probeRemaining;
    vec3 probeSpecular = sampleReflectionProbes(R, fragWorldPos, roughness, probeRemaining);
    prefilteredColor = probeSpecular + prefilteredColor * probeRemaining;
#endif
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

            float shadow = sampleTerrainPointShadowHybrid(light.shadowIndex, light.rtMaskSlice, fragWorldPos, TERRAIN_SHADOW_NORMAL,
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

            float shadow = sampleTerrainSpotShadowHybrid(light.shadowIndex, light.rtMaskSlice, fragWorldPos, TERRAIN_SHADOW_NORMAL);

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

        float shadow = (camera.disableShadows > 0.5)
            ? 1.0
            : sampleTerrainDirectionalShadow(light.shadowIndex, light.shadowMode, fragWorldPos, TERRAIN_SHADOW_NORMAL, linearZ, camera.cameraPos);
        // Fog of war hides the casters (entities are discarded in fog), so fade their ground
        // shadows out by the same mask -> no ghost shadows sitting on top of the fog.
        shadow = mix(1.0, shadow, worldMaskVisibility);
        minShadow = min(minShadow, shadow);

        vec3 lightContrib = evaluateDirectionalLight(N, V, albedo, metallic, roughness, F0, light) * shadow;
#ifdef CAUSTICS_ENABLED
        float caustic = sampleCaustics(causticMap, causticParams.waterHeight, causticParams.causticStrength,
                                       causticParams.depthFalloff, causticParams.patchSize,
                                       fragWorldPos, light.direction);
        lightContrib *= (1.0 + caustic);
#endif
        directLighting += lightContrib;
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

    // VK-1574: global IBL intensity + tint.
    ambient *= camera.iblTintIntensity.a * camera.iblTintIntensity.rgb;

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

    // Overdraw Visualization
    if (viewModeValue == 20u) {
        color = vec3(0.15, 0.4, 0.05);
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
        bool isStampMode = (pc.stampWidth > 0 && pc.stampHeight > 0);

        if (isStampMode) {
            // Stamp: always square bounds
            dist = max(abs(delta.x), abs(delta.y)) / pc.brushWorldRadius;
        } else if (shapeType == 1u) {
            dist = max(abs(delta.x), abs(delta.y)) / pc.brushWorldRadius;
        } else {
            dist = length(delta) / pc.brushWorldRadius;
        }

        // Y proximity check: only show brush on surfaces near the hit point
        float yDist = abs(fragWorldPos.y - pc.brushWorldY);
        float yThreshold = pc.brushWorldRadius * 1.5;
        bool yClose = (yDist <= yThreshold);

        if (dist <= 1.0 && yClose) {
            float falloffValue;
            uint falloffType = uint(pc.brushFalloff);

            if (falloffType == 0u) { falloffValue = 1.0; }
            else if (falloffType == 1u) { falloffValue = 1.0 - dist; }
            else if (falloffType == 2u) { falloffValue = 1.0 - dist*dist*(3.0-2.0*dist); }
            else { falloffValue = pow(1.0 - dist, 3.0); }

            if (isStampMode) {
                // Sample stamp image for overlay shape
                float cosR = cos(pc.stampRotation);
                float sinR = sin(pc.stampRotation);
                vec2 rotatedDelta = vec2(
                    delta.x * cosR + delta.y * sinR,
                    -delta.x * sinR + delta.y * cosR
                );
                vec2 stampUV = rotatedDelta / pc.brushWorldRadius + 0.5;

                if (stampUV.x >= 0.0 && stampUV.x <= 1.0 && stampUV.y >= 0.0 && stampUV.y <= 1.0) {
                    // Bilinear sample
                    float fx = stampUV.x * float(pc.stampWidth - 1);
                    float fz = stampUV.y * float(pc.stampHeight - 1);
                    uint sx0 = uint(fx);
                    uint sz0 = uint(fz);
                    uint sx1 = min(sx0 + 1, pc.stampWidth - 1);
                    uint sz1 = min(sz0 + 1, pc.stampHeight - 1);
                    float fracX = fx - float(sx0);
                    float fracZ = fz - float(sz0);

                    float h00 = stampHeights[sz0 * pc.stampWidth + sx0];
                    float h10 = stampHeights[sz0 * pc.stampWidth + sx1];
                    float h01 = stampHeights[sz1 * pc.stampWidth + sx0];
                    float h11 = stampHeights[sz1 * pc.stampWidth + sx1];

                    float stampValue = mix(mix(h00, h10, fracX), mix(h01, h11, fracX), fracZ);

                    vec3 brushColor = vec3(0.2, 0.6, 1.0);
                    color = mix(color, brushColor, stampValue * falloffValue * 0.5);
                }
            } else {
                vec3 brushColor = vec3(0.2, 0.6, 1.0);
                color = mix(color, brushColor, falloffValue * 0.3);
            }
        }

        if (yClose) {
            float edgeWidth = 0.02;
            float edgeDist = abs(dist - 1.0);
            if (edgeDist < edgeWidth) {
                float edgeAlpha = 1.0 - (edgeDist / edgeWidth);
                vec3 brushColor = vec3(0.2, 0.6, 1.0);
                color = mix(color, brushColor, edgeAlpha * 0.8);
            }
        }
    }

    outColor = vec4(color, 1.0);
}
