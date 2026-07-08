#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragDrawIndex[];
layout(location = 4) flat out uint fragMeshletIndex[];
layout(location = 5) flat out uint fragLodLevel[];
layout(location = 6) flat out vec4 fragInstanceAlbedo[];
layout(location = 7) flat out vec4 fragInstancePBR[];
layout(location = 8) flat out vec4 fragInstanceIBL[];

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
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

layout(std430, set = 5, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};

const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
    mat4 instanceModelMatrix;
    mat4 instanceNormalMatrix;
    uint instanceLodLevel;
    vec4 instanceAlbedo;
    vec4 instancePBR;
    vec4 instanceIBL;
};

taskPayloadSharedEXT MeshletPayload payload;

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
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
    uint drawIndex = payload.drawIndex;
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];
    PerDrawData drawData = perDrawData[drawIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    // Use instance-specific matrices from task shader payload
    mat4 modelMatrix = payload.instanceModelMatrix;
    mat3 normalMatrix = mat3(payload.instanceNormalMatrix);
    mat4 viewProjection = camera.projection * camera.view;

    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;
            uint baseIdx = globalVertexIndex * 16;

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

            if (drawData.boneMatrixOffset != 0xFFFFFFFFu) {
                ivec4 boneIndices = ivec4(
                    floatBitsToInt(vertexData[baseIdx + 8]),
                    floatBitsToInt(vertexData[baseIdx + 9]),
                    floatBitsToInt(vertexData[baseIdx + 10]),
                    floatBitsToInt(vertexData[baseIdx + 11])
                );
                vec4 boneWeights = vec4(
                    vertexData[baseIdx + 12],
                    vertexData[baseIdx + 13],
                    vertexData[baseIdx + 14],
                    vertexData[baseIdx + 15]
                );

                mat4 skinMatrix = mat4(0.0);
                float totalWeight = 0.0;
                for (int i = 0; i < 4; ++i) {
                    int boneIdx = boneIndices[i];
                    float weight = boneWeights[i];
                    if (boneIdx >= 0 && weight > 0.0) {
                        uint globalBoneIdx = drawData.boneMatrixOffset + uint(boneIdx);
                        skinMatrix += boneMatrices[globalBoneIdx] * weight;
                        totalWeight += weight;
                    }
                }

                if (totalWeight > 0.0) {
                    position = (skinMatrix * vec4(position, 1.0)).xyz;
                    normal = normalize(mat3(skinMatrix) * normal);
                }
            }

            sharedPositions[localVertexIndex] = position;
            sharedNormals[localVertexIndex] = normal;
            sharedTexCoords[localVertexIndex] = vec2(
                vertexData[baseIdx + 6],
                vertexData[baseIdx + 7]
            );
        }
    }

    barrier();

    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec4 worldPos = modelMatrix * vec4(sharedPositions[localVertexIndex], 1.0);
            fragWorldPos[localVertexIndex] = worldPos.xyz;
            fragNormal[localVertexIndex] = normalize(normalMatrix * sharedNormals[localVertexIndex]);
            fragTexCoord[localVertexIndex] = sharedTexCoords[localVertexIndex];
            fragDrawIndex[localVertexIndex] = drawIndex;
            fragMeshletIndex[localVertexIndex] = globalMeshletIndex;
            fragLodLevel[localVertexIndex] = payload.instanceLodLevel;
            fragInstanceAlbedo[localVertexIndex] = payload.instanceAlbedo;
            fragInstancePBR[localVertexIndex] = payload.instancePBR;
            fragInstanceIBL[localVertexIndex] = payload.instanceIBL;
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
#include "../common/gi_sampling.glsl"
#include "../common/lod_crossfade.glsl"
#include "../common/wetness.glsl"
#include "../common/snow_accumulation.glsl"

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;
layout(location = 4) in flat uint fragMeshletIndex;
layout(location = 5) in flat uint fragLodLevel;
layout(location = 6) in flat vec4 fragInstanceAlbedo;
layout(location = 7) in flat vec4 fragInstancePBR;
layout(location = 8) in flat vec4 fragInstanceIBL;

layout(location = 0) out vec4 outColor;
#ifdef WBOIT_ENABLED
layout(location = 1) out float outRevealage;
#endif

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilterMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

#ifdef SVT_ENABLED
// VK-1209 material Streamed Virtual Textures. UE5-style: the physical BC7 atlas lives in the
// bindless heap (sampled as bindlessTextures[atlasIndex]); only the page table / feedback /
// per-image info ride the mesh pipeline's OWN set 1 (bindings 3/4/5) — no new descriptor set,
// so the crowded shared set space is untouched. Compiled only when SVT is active.
#include "../common/vt_types.glsl"
layout(std430, set = 1, binding = 3) readonly buffer SVTPageTable { uint svtPageTable[]; };
layout(std430, set = 1, binding = 4) buffer SVTFeedback { uint svtFeedback[]; };
layout(std430, set = 1, binding = 5) readonly buffer SVTImageInfoBuffer { VTImageInfo svtImageInfo[]; };
#define VT_PAGE_TABLE svtPageTable
#define VT_FEEDBACK svtFeedback
#include "../common/vt_sampling.glsl"
const uint SVT_TAG_BIT = 0x80000000u;
#endif

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    uint hiZMipLevels;
} pc;

// Light structs provided by lighting_functions.glsl include

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

struct ClusterGridParams {
    uvec4 gridDimensions;
    vec4 screenParams;
    vec4 depthParams;
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

layout(std140, set = 7, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

struct ClusterLightData {
    uint offset;
    uint counts;
};

layout(std430, set = 8, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 8, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// ShadowData struct (must be before SSBO declaration)
#include "../common/shadow_sampling_types.glsl"

layout(std430, set = 9, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowData[];
};

layout(std430, set = 9, binding = 1) readonly buffer PageTableBuffer {
    uint pageTable[];
};

// Comparison samplers (shadow filtering)
layout(set = 10, binding = 0) uniform sampler2DShadow physicalPoolShadow;
layout(set = 10, binding = 1) uniform sampler2D physicalPoolDepth;

// PCSS sampling functions
#define SHADOW_BUFFER shadowData
#define PAGE_TABLE pageTable
#include "../common/shadow_sampling.glsl"

#ifdef CAUSTICS_ENABLED
layout(set = CAUSTIC_SET, binding = 0) uniform sampler2D causticMap;
layout(set = CAUSTIC_SET, binding = 1) uniform CausticParamsUBO {
    float waterHeight;
    float causticStrength;
    float depthFalloff;
    float patchSize;
    float shoreWetRange;
    float shoreWetDarkening;
    float shoreWetRoughness;
    float pad1;
} causticParams;
#include "../common/caustic_sampling.glsl"
#endif

// All three optional RT shadow masks share one descriptor set (set 13) so sets 15/16 stay free,
// keeping the fragment layout within 14 bound sets. Each binding is declared only under its macro;
// the matching binding in the shared set is written when that RT type is online (PARTIALLY_BOUND).
#ifdef RT_SHADOW_ENABLED
layout(set = 13, binding = 0) uniform sampler2D rtShadowMask;                 // directional (VK-1150)
#endif

#ifdef RT_SPOT_SHADOW_ENABLED
// Per-spot-light RT shadow masks (VK-1175). One array slice per budgeted spot light; a light's
// GPUSpotLight.rtMaskSlice indexes the slice (-1 = stays on VSM).
layout(set = 13, binding = 1) uniform sampler2DArray rtSpotShadowMaskArray;
#endif

#ifdef RT_POINT_SHADOW_ENABLED
// Per-point-light RT shadow masks (VK-1176). One array slice per budgeted point light; a light's
// GPUPointLight.rtMaskSlice indexes the slice (-1 = stays on VSM).
layout(set = 13, binding = 2) uniform sampler2DArray rtPointShadowMaskArray;
#endif

#ifdef WORLD_MASK_ENABLED
// Plugin world-space mask (VK-1359): XZ-projected over worldMinMax bounds.
// WORLD_MASK_SET is 11, or 14 when GI probes occupy set 11.
layout(set = WORLD_MASK_SET, binding = 0) uniform sampler2D worldMaskTexture;
layout(set = WORLD_MASK_SET, binding = 1) uniform WorldMaskUBO {
    vec4 worldMinMax;          // minX, minZ, maxX, maxZ
    float terrainDimMin;
    float entityDiscardBelow;
    uint flags;                // bit0 enabled, bit1 affectsTerrain, bit2 affectsEntities
    float _padWM;
} worldMask;
#endif

float sampleDirectionalShadowHybrid(int shadowIndex, int shadowMode,
                                     vec3 worldPos, vec3 N, float viewZ, vec3 cameraPos) {
#ifdef RT_SHADOW_ENABLED
    // Optional RT override (off by default): when active, the ray-traced mask wins full-screen.
    if (lightCounts.rtShadowActive != 0u) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtShadowMask, screenUV).r;
    }
#endif
    if (shadowIndex < 0) return 1.0;
    // shadowMode 1 = VSM clipmap (the unified default directional shadow on all GPUs).
    if (shadowMode == 1) return sampleDirectionalVSM(shadowIndex, worldPos, N);
    return 1.0;
}

float sampleSpotShadowHybrid(int shadowIndex, int rtMaskSlice, vec3 worldPos, vec3 N) {
#ifdef RT_SPOT_SHADOW_ENABLED
    // Optional RT override (off by default): when active and this light owns a mask slice,
    // the ray-traced mask wins; otherwise fall through to the always-available VSM base.
    if (lightCounts.rtSpotShadowActive != 0u && rtMaskSlice >= 0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtSpotShadowMaskArray, vec3(screenUV, float(rtMaskSlice))).r;
    }
#endif
    return sampleSpotShadow(shadowIndex, worldPos, N);
}

float samplePointShadowHybrid(int baseShadowIndex, int rtMaskSlice, vec3 worldPos, vec3 N,
                              vec3 lightPos, float lightRadius) {
#ifdef RT_POINT_SHADOW_ENABLED
    // Optional RT override (off by default): when active and this light owns a mask slice,
    // the ray-traced mask wins; otherwise fall through to the always-available VSM base.
    if (lightCounts.rtPointShadowActive != 0u && rtMaskSlice >= 0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(pc.screenWidth, pc.screenHeight);
        return texture(rtPointShadowMaskArray, vec3(screenUV, float(rtMaskSlice))).r;
    }
#endif
    return samplePointShadow(baseShadowIndex, worldPos, N, lightPos, lightRadius);
}

const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu;

float linearizeDepth(float windowZ) {
    float near = clusterParams.depthParams.x;
    float far = clusterParams.depthParams.y;
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

uint getClusterIndex(vec2 fragCoord, float viewZ) {
    float clampedZ = max(viewZ, clusterParams.depthParams.x);

    uint tileX = uint(fragCoord.x / clusterParams.screenParams.z);
    uint tileY = uint(fragCoord.y / clusterParams.screenParams.w);

    float logRatio = log(clampedZ / clusterParams.depthParams.x);
    uint slice = uint(logRatio * clusterParams.depthParams.w);

    tileX = min(tileX, clusterParams.gridDimensions.x - 1u);
    tileY = min(tileY, clusterParams.gridDimensions.y - 1u);
    slice = min(slice, clusterParams.gridDimensions.z - 1u);

    return tileX + tileY * clusterParams.gridDimensions.x +
           slice * clusterParams.gridDimensions.x * clusterParams.gridDimensions.y;
}

const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
const uint FLAG_ALPHA_MASK = 1u << 4;
const uint FLAG_TRANSLUCENT = 1u << 5;
const uint FLAG_ADDITIVE_BLEND = 1u << 10;

bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

#ifdef SVT_ENABLED
// VK-1482: INVALID_TEXTURE_INDEX (0xFFFFFFFF) has bit 31 set and would alias SVT_TAG_BIT — an
// unbound material slot must never route into the SVT path (it OOB-read svtImageInfo[0x7FFFFFFF]
// and OOB-atomicOr'd the feedback buffer, corrupting ao/normal/emission on every unbound slot).
// C++ producers only ever emit a bindless slot (bit 31 clear), SVT_TAG_BIT | imageId, or the
// INVALID_TEXTURE_INDEX / 0xFFu sentinels, so excluding the sentinel exactly isolates real tags.
// Mirror: render::gpudriven::svtIsTaggedIndex (SVTManager.hpp).
bool isSVTTagged(uint index) {
    return index != INVALID_TEXTURE_INDEX && (index & SVT_TAG_BIT) != 0u;
}
#endif

// A material texture index is sampleable if it's a normal bindless index OR (SVT on) an
// SVT-tagged index. When SVT is off this is exactly isValidTexture (byte-identical).
bool isSampleableTexture(uint index) {
#ifdef SVT_ENABLED
    if (isSVTTagged(index)) return true;
#endif
    return isValidTexture(index);
}

// Sample a material texture. SVT-tagged indices resolve through the page table into the bindless
// atlas (with a whole-image fallback while a page streams in) and emit a feedback request; all
// other indices are a plain bindless sample. Off = plain bindless sample.
vec4 sampleMaterialTex(uint index, vec2 uv, vec2 dx, vec2 dy) {
#ifdef SVT_ENABLED
    if (isSVTTagged(index)) {
        VTImageInfo img = svtImageInfo[index & 0x7FFFFFFFu];
        uint atlasIndex = img.pad0;
        uint fallbackIndex = img.pad1;
        // VK-1480: wrap the UV into [0,1) for the page lookup + feedback so tiled (UV>1) materials
        // request/sample the correct pages. Keep the ORIGINAL uv/derivatives for the desired-mip
        // estimate and the whole-image fallback — fract() at a wrap seam produces derivative spikes
        // that would otherwise pick the wrong mip.
        vec2 wuv = fract(uv);
        uint mip = uint(max(vtDesiredMip(uv, float(img.pagesX0 * VT_PAGE_INTERIOR)), 0.0));
        if (vtFeedbackFragment(gl_FragCoord.xy))
            vtWriteFeedback(img, wuv, mip);
        VTSample s = vtLookup(img, wuv, mip);
        if (s.valid)
            return textureLod(bindlessTextures[nonuniformEXT(atlasIndex)], s.uv, 0.0);
        return textureGrad(bindlessTextures[nonuniformEXT(fallbackIndex)], uv, dx, dy);
    }
#endif
    return textureGrad(bindlessTextures[nonuniformEXT(index)], uv, dx, dy);
}

vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

void main() {
#ifdef WORLD_MASK_ENABLED
    // Plugin world mask: discard fragments where the XZ-projected mask is below the
    // threshold (e.g. fog-of-war hidden entities). Outside the bounds = unaffected.
    if ((worldMask.flags & 5u) == 5u) {   // enabled & affectsEntities
        vec2 maskUV = (fragWorldPos.xz - worldMask.worldMinMax.xy)
                    / (worldMask.worldMinMax.zw - worldMask.worldMinMax.xy);
        if (all(greaterThanEqual(maskUV, vec2(0.0))) && all(lessThanEqual(maskUV, vec2(1.0)))) {
            if (texture(worldMaskTexture, maskUV).r < worldMask.entityDiscardBelow) {
                discard;
            }
        }
    }
#endif

    PerDrawData drawData = perDrawData[fragDrawIndex];

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(camera.cameraPos - fragWorldPos);

    uint albedoIdx = drawData.textureIndices0.x;
    uint normalIdx = drawData.textureIndices0.y;
    uint ormIdx = drawData.textureIndices0.z;
    uint metallicIdx = drawData.textureIndices0.w;
    uint roughnessIdx = drawData.textureIndices1.x;
    uint aoIdx = drawData.textureIndices1.y;
    uint emissionIdx = drawData.textureIndices1.z;

    vec2 texCoords = fragTexCoord;
    if (drawData.shaderGroupIndex == 1u) {
        texCoords += vec2(camera.time * 0.1, 0.0);
    }

    vec2 texDx = dFdx(fragTexCoord);
    vec2 texDy = dFdy(fragTexCoord);

    // Per-instance PBR override (when hasOverride flag is set)
    vec4 matAlbedo = drawData.albedo;
    vec4 matParams = drawData.materialParams;
    float matIblDiffuse = drawData.iblDiffuse;
    float matIblSpecular = drawData.iblSpecular;

    if (fragInstanceIBL.w > 0.5) {
        matAlbedo = fragInstanceAlbedo;
        matParams = fragInstancePBR;
        matIblDiffuse = fragInstanceIBL.x;
        matIblSpecular = fragInstanceIBL.y;
    }

    vec3 albedo = matAlbedo.rgb;
    float alpha = matAlbedo.a;

    if (isSampleableTexture(albedoIdx)) {
        vec4 albedoSample = sampleMaterialTex(albedoIdx, texCoords, texDx, texDy);
        albedo = albedoSample.rgb;
        alpha = albedoSample.a;
#ifdef SVT_ENABLED
        // VK-1480: an SVT albedo tile can carry alpha ~= 0 (BC7 alpha in uncovered/streaming texels),
        // which the color*alpha output premultiply would collapse to black. Opaque materials do not
        // use albedo alpha, so force full opacity for them; alpha-mask/translucent/additive keep it.
        if (isSVTTagged(albedoIdx) &&
            (drawData.flags & (FLAG_ALPHA_MASK | FLAG_TRANSLUCENT | FLAG_ADDITIVE_BLEND)) == 0u) {
            alpha = 1.0;
        }
#endif
    }

    if ((drawData.flags & FLAG_ALPHA_MASK) != 0u) {
        float alphaCutoff = float((drawData.blendModeAndOpacity >> 8u) & 0xFFu) / 255.0;
        if (alpha < alphaCutoff) {
            discard;
        }
    }

    // LOD crossfade dithering — skip for translucent objects (dither + alpha blend = holes)
    {
        float crossfadeAlpha = extractCrossfadeAlpha(fragLodLevel);
        if (crossfadeAlpha > 0.0 && (drawData.flags & FLAG_TRANSLUCENT) == 0u) {
            if (ditherTest(gl_FragCoord.xy, crossfadeAlpha)) {
                discard;
            }
        }
    }

    if ((drawData.flags & FLAG_TRANSLUCENT) != 0u) {
        float materialOpacity = float(drawData.blendModeAndOpacity >> 16u) / 65535.0;
        alpha *= materialOpacity;
    }

    float metallic = matParams.x;
    float roughness = matParams.y;
    float ao = matParams.z;
    float emission = matParams.w;

    if (isSampleableTexture(ormIdx)) {
        vec3 ormValues = unpackORM(sampleMaterialTex(ormIdx, texCoords, texDx, texDy));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        if (isSampleableTexture(metallicIdx)) {
            metallic = sampleMaterialTex(metallicIdx, texCoords, texDx, texDy).r;
        }
        if (isSampleableTexture(roughnessIdx)) {
            roughness = sampleMaterialTex(roughnessIdx, texCoords, texDx, texDy).r;
        }
        if (isSampleableTexture(aoIdx)) {
            ao = sampleMaterialTex(aoIdx, texCoords, texDx, texDy).r;
        }
    }

    if (isSampleableTexture(normalIdx)) {
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        vec3 tangentNormal = sampleMaterialTex(normalIdx, texCoords, texDx, texDy).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    // Weather surface effects (wetness first, then snow on top)
    applyWetness(camera.wetness, albedo, roughness, metallic, N);
    applySnowAccumulation(camera.snowAccumulation, fragNormal, albedo, roughness, metallic, N);

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;

    vec3 specularScale;
    vec3 kD;
    multiScatterCompensation(F0, brdf, metallic, specularScale, kD);

    vec3 diffuse = irradiance * albedo * matIblDiffuse;
    vec3 specular = prefilteredColor * specularScale * matIblSpecular;

    float so = specularOcclusion(NdotV, ao, roughness);
    vec3 ambient = kD * diffuse * ao + specular * so;

    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;

    float linearZ = linearizeDepth(gl_FragCoord.z);

    if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
        uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);

        ClusterLightData clusterData = clusterLightGrid[clusterIdx];
        uint clusterPointCount = clusterData.counts & 0xFFFFu;
        uint clusterSpotCount = clusterData.counts >> 16u;
        uint lightOffset = clusterData.offset;

        for (uint i = 0u; i < clusterPointCount; ++i) {
            uint lightIdx = lightIndexList[lightOffset + i];
            PointLight light = pointLights[lightIdx];
            float shadow = samplePointShadowHybrid(light.shadowIndex, light.rtMaskSlice, fragWorldPos, N, light.position, light.radius);
            minShadow = min(minShadow, shadow);
            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }

        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
            SpotLight light = spotLights[lightIdx];
            float shadow = sampleSpotShadowHybrid(light.shadowIndex, light.rtMaskSlice, fragWorldPos, N);
            minShadow = min(minShadow, shadow);
            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }
    }

    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        float shadow = (camera.disableShadows > 0.5)
            ? 1.0
            : sampleDirectionalShadowHybrid(light.shadowIndex, light.shadowMode, fragWorldPos, N, linearZ, camera.cameraPos);
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

    float emissionMultiplier = emission;
    if (drawData.shaderGroupIndex == 2u) {
        emissionMultiplier *= cos(camera.time);
    }

    vec3 emissive = vec3(0.0);
    if (isSampleableTexture(emissionIdx)) {
        emissive = sampleMaterialTex(emissionIdx, texCoords, texDx, texDy).rgb * emissionMultiplier;
    } else {
        emissive = albedo * emissionMultiplier;
    }

    vec3 giContribution = vec3(0.0);
#ifdef GI_ENABLED
    float cameraDist = length(camera.cameraPos - fragWorldPos);
    vec3 giIrradiance = sampleProbeGI(fragWorldPos, N, cameraDist);
    giContribution = giIrradiance * albedo * kD;
    // Reduce ambient proportionally to GI strength to avoid double-counting
    // When GI is zero (probes not converged), ambient stays full
    float giStrength = min(length(giIrradiance), 1.0);
    ambient *= mix(1.0, 0.3, giStrength);
#endif

    vec3 color = ambient + directLighting + giContribution + emissive;

    uint viewModeValue = pc.viewMode & 0xFFu;

    if (viewModeValue == 1u) {
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
        vec3 lodColors[4] = vec3[4](
            vec3(0.0, 1.0, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(1.0, 0.5, 0.0),
            vec3(1.0, 0.0, 0.0)
        );
        uint lod = min(fragLodLevel & 0xFFu, 3u);
        color = mix(color, lodColors[lod], 0.5);
    }

    if (viewModeValue == 3u) {
        float dx = max(length(texDx), length(texDy));
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
        vec3 mipColor = mix(mipColors[idx], mipColors[idx + 1], fract(t));
        color = mipColor;
    }

    if (viewModeValue == 4u) {
        uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);

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
        float totalShadow = 1.0;

        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            float shadow = sampleDirectionalShadowHybrid(light.shadowIndex, light.shadowMode, fragWorldPos, N, linearZ, camera.cameraPos);
            totalShadow = min(totalShadow, shadow);
        }

        if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
            uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);
            ClusterLightData clusterData = clusterLightGrid[clusterIdx];
            uint clusterPointCount = clusterData.counts & 0xFFFFu;
            uint clusterSpotCount = clusterData.counts >> 16u;
            uint lightOffset = clusterData.offset;

            for (uint i = 0u; i < clusterPointCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + i];
                uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
                PointLight light = pointLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, N, light.position, light.radius);
                    totalShadow = min(totalShadow, shadow);
                }
            }

            for (uint i = 0u; i < clusterSpotCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
                uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
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

    // Clipmap/Cascade Level Visualization
    if (viewModeValue == 14u) {
        color = vec3(0.2);
        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            if (light.shadowIndex < 0) continue;

            int levelCount = int(SHADOW_BUFFER[light.shadowIndex].rangeParams.z);
            float baseExtent = SHADOW_BUFFER[light.shadowIndex].rangeParams.x;
            float worldDist = length(fragWorldPos - camera.cameraPos);

            if (light.shadowMode == 1) {
                // Clipmap: level by world distance
                float lvl = max(log2(max(worldDist, baseExtent) / baseExtent), 0.0);
                int levelIdx = clamp(int(lvl), 0, levelCount - 1);
                float t = float(levelIdx) / max(float(levelCount - 1), 1.0);
                // Blue(near) → Cyan → Green → Yellow → Red(far)
                color = mix(vec3(0.0, 0.2, 1.0), vec3(1.0, 0.2, 0.0), t);
                // Show level boundaries as bright lines
                float frac = fract(lvl);
                if (frac < 0.02 || frac > 0.98) color = vec3(1.0);
            } else {
                // CSM: cascade by view depth
                float linearZ = linearizeDepth(gl_FragCoord.z);
                vec3 cascadeColors[4] = vec3[](
                    vec3(1.0, 0.2, 0.2), vec3(1.0, 0.7, 0.2),
                    vec3(0.9, 0.9, 0.2), vec3(0.2, 1.0, 0.3));
                int cascadeIdx = 0;
                for (int c = 0; c < min(levelCount, 4); ++c) {
                    if (linearZ < SHADOW_BUFFER[light.shadowIndex + c].rangeParams.y) {
                        cascadeIdx = c; break;
                    }
                    cascadeIdx = c;
                }
                color = cascadeColors[cascadeIdx];
            }
        }
    }

    // Shadow UV Visualization
    if (viewModeValue == 15u) {
        color = vec3(0.0);
        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            if (light.shadowIndex < 0) continue;
            ShadowData sd = SHADOW_BUFFER[light.shadowIndex];
            vec4 lsPos = sd.viewProjection * vec4(fragWorldPos, 1.0);
            vec2 uv = lsPos.xy * 0.5 + 0.5;
            // Red = U, Green = V, Blue = valid page
            bool valid;
            vec2 physUV = vsmLookupPhysicalUV(sd, uv, valid);
            color = vec3(uv.x, uv.y, valid ? 0.5 : 0.0);
        }
    }

    // Overdraw Visualization - flat per-fragment color for heatmap accumulation
    if (viewModeValue == 20u) {
        // Output a flat low-intensity color per fragment.
        // With additive-like accumulation, overlapping fragments produce brighter colors.
        // Single coverage: green tint. Multiple: shifts toward yellow/red.
        float depth01 = gl_FragCoord.z;
        color = vec3(0.15, 0.4, 0.05); // Base green per fragment
    }

    // Ambient-input probe (viewMode 30, VK-1482) — works with SVT on OR off so the two can be compared.
    // R = matIblDiffuse (per-material IBL diffuse scale), G = irradiance brightness (IBL cubemap sample),
    // B = ao. The diffuse ambient term is proportional to R*G*B*albedo, so whichever channel drops when a
    // material darkens is the culprit input. alpha forced to 1 so it survives the output premultiply.
    if (viewModeValue == 30u) {
        float irr = max(max(irradiance.r, irradiance.g), irradiance.b);
        color = vec3(matIblDiffuse, irr, ao);
        alpha = 1.0;
    }

#ifdef WBOIT_ENABLED
    // Weighted Blended OIT (McGuire & Bavoil 2013)
    float viewZ = linearizeDepth(gl_FragCoord.z);
    float w = alpha * max(1e-2, min(3e3, 10.0 / (1e-5 + pow(viewZ / 200.0, 4.0))));
    outColor = vec4(color * alpha * w, alpha * w);
    outRevealage = alpha;
#else
    bool isAdditive = (drawData.flags & FLAG_ADDITIVE_BLEND) != 0u;
    if (isAdditive) {
        outColor = vec4(color * alpha, 0.0);
    } else {
        outColor = vec4(color * alpha, alpha);
    }
#endif
}
