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

// Raw float array: 16 floats per vertex (position.xyz, normal.xyz, texCoord.xy, boneIndices.xyzw, boneWeights.xyzw)
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

    mat4 modelMatrix = drawData.modelMatrix;
    mat3 normalMatrix = mat3(drawData.normalMatrix);
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

            // Apply GPU skinning if this is an animated mesh
            if (drawData.boneMatrixOffset != 0xFFFFFFFFu) {
                // Read bone indices (stored as floats, need to reinterpret as ints)
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

                // Only apply skinning if we have valid bone influences
                // If no bones contributed, leave position/normal unchanged
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

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in flat uint fragDrawIndex;
layout(location = 4) in flat uint fragMeshletIndex;

layout(location = 0) out vec4 outColor;

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

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

// Set 6: Light Data

// Matches GPUDirectionalLight in GPULightTypes.hpp (32 bytes)
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    int shadowIndex;  // Index into shadow data SSBO, -1 = no shadow
};

layout(std430, set = 6, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight directionalLights[];
};

// Matches GPUPointLight in GPULightTypes.hpp (48 bytes)
struct PointLight {
    vec3 position;
    float radius;
    vec3 color;
    float intensity;
    int shadowIndex;      // Index to cube map in shadow data, -1 = no shadow
    uint padding0;
    uint padding1;
    uint padding2;
};

layout(std430, set = 6, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};

// Matches GPUSpotLight in GPULightTypes.hpp (64 bytes)
struct SpotLight {
    vec3 position;
    float range;
    vec3 direction;
    float intensity;
    vec3 color;
    float cosInnerAngle;
    float cosOuterAngle;
    int shadowIndex;      // Index into shadow data SSBO, -1 = no shadow
    uint padding0;
    uint padding1;
};

layout(std430, set = 6, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

struct LightCounts {
    uint directionalCount;
    uint pointCount;
    uint spotCount;
    float shadowIntensity;  // Controls ambient occlusion in shadowed areas (0-1)
};

layout(std140, set = 6, binding = 3) uniform LightCountsUBO {
    LightCounts lightCounts;
};

// Set 7: Cluster Grid Params
struct ClusterGridParams {
    uvec4 gridDimensions;  // xyz = tilesX, tilesY, slicesZ, w = totalClusters
    vec4 screenParams;     // xy = screenSize, zw = tileSizePixels
    vec4 depthParams;      // x = near, y = far, z = log(far/near), w = slicesZ/log(far/near)
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

layout(std140, set = 7, binding = 0) uniform ClusterParamsUBO {
    ClusterGridParams clusterParams;
};

// Set 8: Light Culling Output
struct ClusterLightData {
    uint offset;
    uint counts;   // lower 16 bits = point count, upper 16 bits = spot count
};

layout(std430, set = 8, binding = 0) readonly buffer ClusterLightGridBuffer {
    ClusterLightData clusterLightGrid[];
};

layout(std430, set = 8, binding = 1) readonly buffer ClusterLightIndexListBuffer {
    uint lightIndexList[];
};

// Set 9: Shadow Data SSBO
// Matches GPUShadowData in ShadowTypes.hpp (128 bytes)
struct ShadowData {
    mat4 viewProjection;      // 64 bytes - light space transform
    vec4 atlasViewport;       // 16 bytes - xy=offset, zw=size (CSM: z=cascadeCount)
    vec4 biasParams;          // 16 bytes - x=depthBias, y=slopeBias, z=normalBias, w=texelSize
    vec4 rangeParams;         // 16 bytes - x=near, y=far, z=1/(far-near), w=cascadeIndex
    vec4 pcfParams;           // 16 bytes - x=kernelSize (0-4: 1x1 to 5x5), y=softness, z=filterEnabled, w=reserved
};

layout(std430, set = 9, binding = 0) readonly buffer ShadowDataBuffer {
    ShadowData shadowData[];
};

// Set 10: Shadow Textures
layout(set = 10, binding = 0) uniform sampler2DShadow shadowAtlas;         // Spot light atlas
layout(set = 10, binding = 1) uniform sampler2DArrayShadow shadowCascades; // CSM cascade array
layout(set = 10, binding = 2) uniform samplerCubeShadow shadowCubes[];     // Point light cube maps

const float LIGHTING_PI = 3.14159265359;

// Light index packing scheme for the cluster light index list:
// - Point light indices are stored as-is (bits 0-30 = index)
// - Spot light indices have the high bit (bit 31) set to distinguish them
// This allows both light types to share the same index list while being identifiable
const uint SPOT_LIGHT_FLAG = 0x80000000u;  // High bit flag for spot lights
const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu; // Mask to extract the actual light index

// Convert window-space depth to linear view-space depth
// Assumes Vulkan's standard [0, 1] depth range with default viewport settings
// where gl_FragCoord.z maps directly to [0, 1] (near to far)
// Formula derivation: z_view = near * far / (far - z_ndc * (far - near))
float linearizeDepth(float windowZ) {
    float near = clusterParams.depthParams.x;
    float far = clusterParams.depthParams.y;
    // Guard against division by zero (occurs if windowZ > 1, which shouldn't happen)
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

// Calculate cluster index from fragment position
uint getClusterIndex(vec2 fragCoord, float viewZ) {
    // Clamp viewZ to near plane to prevent undefined log() behavior
    // This handles fragments at or behind the camera
    float clampedZ = max(viewZ, clusterParams.depthParams.x);

    // Tile coordinates from screen position
    uint tileX = uint(fragCoord.x / clusterParams.screenParams.z);
    uint tileY = uint(fragCoord.y / clusterParams.screenParams.w);

    // Slice from logarithmic depth
    float logRatio = log(clampedZ / clusterParams.depthParams.x);
    uint slice = uint(logRatio * clusterParams.depthParams.w);

    // Clamp to valid range
    tileX = min(tileX, clusterParams.gridDimensions.x - 1u);
    tileY = min(tileY, clusterParams.gridDimensions.y - 1u);
    slice = min(slice, clusterParams.gridDimensions.z - 1u);

    // Linear index: x + y * tilesX + z * tilesX * tilesY
    return tileX + tileY * clusterParams.gridDimensions.x +
           slice * clusterParams.gridDimensions.x * clusterParams.gridDimensions.y;
}

// Smooth distance attenuation with range falloff
float smoothDistanceAttenuation(float distance, float range) {
    float distRatio = distance / range;
    float attenuation = clamp(1.0 - distRatio * distRatio, 0.0, 1.0);
    return attenuation * attenuation;
}

// Physical distance attenuation (inverse square with smooth cutoff)
// Intensity scale: makes intensity=1 equivalent to a bright light at 1 meter distance
// Without this, you'd need intensity=10000+ to see anything at typical distances
const float LIGHT_INTENSITY_SCALE = 100.0;

float physicalAttenuation(float distance, float range) {
    float windowFn = smoothDistanceAttenuation(distance, range);
    float distAtt = LIGHT_INTENSITY_SCALE / max(distance * distance, 0.0001);
    return distAtt * windowFn;
}

float spotAngleAttenuation(vec3 lightDir, vec3 spotDir, float cosInner, float cosOuter) {
    float cosAngle = dot(-lightDir, spotDir);

    // Handle degenerate case: hard-edged spotlight with no falloff zone
    if (cosInner <= cosOuter) {
        return cosAngle >= cosOuter ? 1.0 : 0.0;
    }

    return clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
}

// ============================================
// Shadow Sampling Functions
// ============================================

// Spot light shadow (2D atlas with PCF 3x3)
float sampleSpotShadow(int shadowIndex, vec3 worldPos) {
    if (shadowIndex < 0) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    // Transform world position to light space
    vec4 lightSpacePos = sd.viewProjection * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform XY from NDC [-1,1] to texture coords [0,1]
    // Z is already in [0,1] because GLM_FORCE_DEPTH_ZERO_TO_ONE is defined
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Apply atlas viewport transformation
    projCoords.xy = sd.atlasViewport.xy + projCoords.xy * sd.atlasViewport.zw;

    // Apply depth bias
    float bias = sd.biasParams.x;
    projCoords.z -= bias;

    // Out of shadow map bounds check
    if (projCoords.z > 1.0 || projCoords.z < 0.0) return 1.0;
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0)))) return 1.0;

    // Check if PCF filtering is enabled
    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);  // 0=1x1, 1=2x2, 2=3x3, 3=4x4, 4=5x5

    if (!filterEnabled || kernelSize == 0) {
        // Hard shadows - single sample
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    // PCF filtering with configurable kernel size
    float shadow = 0.0;
    float texelSize = sd.biasParams.w;
    float softness = sd.pcfParams.y;
    float spread = texelSize * softness;
    int sampleCount = 0;
    int size = kernelSize + 1;  // 2x2, 3x3, 4x4, or 5x5
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

// Helper function to sample a single cascade with PCF filtering
// Used by sampleDirectionalShadow for cascade blending
float sampleCascadeShadow(int shadowIndex, vec3 worldPos) {
    ShadowData sd = shadowData[shadowIndex];

    // Transform world position to light space
    vec4 lightSpacePos = sd.viewProjection * vec4(worldPos, 1.0);

    // Safety check for orthographic projection (w should be 1.0)
    if (lightSpacePos.w <= 0.0) return 1.0;

    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform XY from NDC [-1,1] to texture coords [0,1]
    // Z is already in [0,1] because GLM_FORCE_DEPTH_ZERO_TO_ONE is defined
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;

    // Clamp to valid range - objects outside frustum get clamped to edge
    texCoords = clamp(texCoords, 0.0, 1.0);

    // Apply atlas viewport transformation (each cascade is a tile in the atlas)
    projCoords.xy = sd.atlasViewport.xy + texCoords * sd.atlasViewport.zw;

    // Apply depth bias (varies per cascade)
    projCoords.z = clamp(projCoords.z - sd.biasParams.x, 0.0, 1.0);

    // Check if PCF filtering is enabled
    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);  // 0=1x1, 1=2x2, 2=3x3, 3=4x4, 4=5x5

    if (!filterEnabled || kernelSize == 0) {
        // Hard shadows - single sample from atlas
        return texture(shadowAtlas, vec3(projCoords.xy, projCoords.z));
    }

    // PCF filtering with configurable kernel size
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

// Directional light shadow (CSM with cascade selection and blending)
// Features: cascade blending for smooth transitions, distance fade
float sampleDirectionalShadow(int baseShadowIndex, vec3 worldPos, float viewZ) {
    if (baseShadowIndex < 0) return 1.0;  // No shadow data - fully lit

    // Read cascade count from first cascade's rangeParams.z
    int cascadeCount = int(shadowData[baseShadowIndex].rangeParams.z);
    cascadeCount = clamp(cascadeCount, 1, 4);  // Safety clamp

    // Select cascade based on view depth
    // The rangeParams.y stores the far plane for each cascade
    int cascadeIdx = 0;
    for (int i = 0; i < cascadeCount; ++i) {
        if (viewZ < shadowData[baseShadowIndex + i].rangeParams.y) {
            cascadeIdx = i;
            break;
        }
        cascadeIdx = i;  // Use furthest cascade if beyond all
    }

    int shadowIndex = baseShadowIndex + cascadeIdx;
    float cascadeFar = shadowData[shadowIndex].rangeParams.y;

    // Sample current cascade
    float shadow = sampleCascadeShadow(shadowIndex, worldPos);

    // Blend with next cascade in transition zone (last 10% of cascade range)
    float blendZoneStart = cascadeFar * 0.9;
    if (viewZ > blendZoneStart && cascadeIdx < cascadeCount - 1) {
        float nextShadow = sampleCascadeShadow(shadowIndex + 1, worldPos);
        float blendFactor = smoothstep(blendZoneStart, cascadeFar, viewZ);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    // Distance fade for last cascade - smooth transition to fully lit at max distance
    float maxDistance = shadowData[baseShadowIndex + cascadeCount - 1].rangeParams.y;
    float fadeStart = maxDistance * 0.85;
    float fadeFactor = 1.0 - smoothstep(fadeStart, maxDistance, viewZ);

    return mix(1.0, shadow, fadeFactor);
}

// Point light shadow (cube map sampling with optional PCF)
float samplePointShadow(int shadowIndex, vec3 worldPos, vec3 lightPos, float lightRadius) {
    if (shadowIndex < 0) return 1.0;

    ShadowData sd = shadowData[shadowIndex];

    // Get the cube map index from pcfParams.w (-1 means no cube map assigned)
    int cubeMapIndex = int(sd.pcfParams.w);
    if (cubeMapIndex < 0) return 1.0;

    // Direction from light to fragment (used to sample cube map)
    vec3 lightToFrag = worldPos - lightPos;
    float linearDepth = length(lightToFrag);  // Linear distance from light
    vec3 sampleDir = normalize(lightToFrag);

    float near = sd.rangeParams.x;
    float far = sd.rangeParams.y;

    // For cube maps, the depth stored corresponds to the view-space Z of the selected face.
    // The major axis component of the direction determines which face is selected.
    // The view-space Z = linearDepth * majorComponent (since direction is normalized).
    float majorComponent = max(abs(sampleDir.x), max(abs(sampleDir.y), abs(sampleDir.z)));
    float viewSpaceZ = linearDepth * majorComponent;

    // Convert view-space depth to perspective depth for comparison
    // The cube map stores perspective depth: depth = (far * (z - near)) / (z * (far - near))
    float perspectiveDepth = (far * (viewSpaceZ - near)) / (viewSpaceZ * (far - near));

    // Apply depth bias
    perspectiveDepth -= sd.biasParams.x;

    // Check if PCF filtering is enabled
    bool filterEnabled = sd.pcfParams.z > 0.5;
    int kernelSize = int(sd.pcfParams.x);  // 0=1x1, 1=2x2, 2=3x3, 3=4x4, 4=5x5

    if (!filterEnabled || kernelSize == 0) {
        // Hard shadows - single sample
        return texture(shadowCubes[nonuniformEXT(cubeMapIndex)], vec4(sampleDir, perspectiveDepth));
    }

    // PCF for cube maps - sample with direction offsets along tangent basis
    float softness = sd.pcfParams.y;
    float spread = softness * 0.01;  // Smaller spread for cube maps

    // Build tangent basis from sample direction
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

// GGX/Trowbridge-Reitz Normal Distribution Function
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (LIGHTING_PI * denom * denom);
}

// Schlick-GGX Geometry function
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's combined geometry term
float geometrySmith(float NdotV, float NdotL, float roughness) {
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
vec3 fresnelSchlickDirect(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Evaluate point light PBR contribution
vec3 evaluatePointLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                        float metallic, float roughness, vec3 F0,
                        PointLight light) {
    vec3 L = light.position - worldPos;
    float distance = length(L);

    if (distance > light.radius) return vec3(0.0);

    L = normalize(L);

    // Early exit if surface faces away from light (avoids expensive BRDF calculations)
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float attenuation = physicalAttenuation(distance, light.radius);
    vec3 radiance = light.color * light.intensity * attenuation;

    // Cook-Torrance BRDF
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

vec3 evaluateSpotLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo,
                       float metallic, float roughness, vec3 F0,
                       SpotLight light) {
    vec3 L = light.position - worldPos;
    float distance = length(L);

    if (distance > light.range) return vec3(0.0);

    L = normalize(L);

    float spotAtt = spotAngleAttenuation(L, light.direction, light.cosInnerAngle, light.cosOuterAngle);
    if (spotAtt <= 0.0) return vec3(0.0);

    // Early exit if surface faces away from light (avoids expensive BRDF calculations)
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float distAtt = physicalAttenuation(distance, light.range);
    vec3 radiance = light.color * light.intensity * distAtt * spotAtt;

    // Cook-Torrance BRDF
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

vec3 evaluateDirectionalLight(vec3 N, vec3 V, vec3 albedo,
                              float metallic, float roughness, vec3 F0,
                              DirectionalLight light) {
    vec3 L = -normalize(light.direction);

    // Early exit if surface faces away from light (avoids expensive BRDF calculations)
    float NdotL = dot(N, L);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    vec3 radiance = light.color * light.intensity;

    // Cook-Torrance BRDF
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlickDirect(HdotV, F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specularBRDF = numerator / denominator;

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / LIGHTING_PI + specularBRDF) * radiance * NdotL;
}

const float ALPHA_CUTOFF = 0.5;
const float MAX_REFLECTION_LOD = 4.0;
const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
const uint FLAG_ALPHA_MASK = 1u << 4;

bool isValidTexture(uint index) {
    return index != INVALID_TEXTURE_INDEX && index != 0xFFu && index < 4096u;
}

vec3 unpackORM(vec4 ormSample) {
    return vec3(ormSample.r, ormSample.g, ormSample.b);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
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

    vec3 albedo = drawData.albedo.rgb;
    float alpha = drawData.albedo.a;

    if (isValidTexture(albedoIdx)) {
        vec4 albedoSample = textureGrad(bindlessTextures[nonuniformEXT(albedoIdx)], texCoords, texDx, texDy);
        albedo = pow(albedoSample.rgb, vec3(2.2));
        alpha = albedoSample.a;
    }

    if ((drawData.flags & FLAG_ALPHA_MASK) != 0u) {
        if (alpha < ALPHA_CUTOFF) {
            discard;
        }
    }

    float metallic = drawData.materialParams.x;
    float roughness = drawData.materialParams.y;
    float ao = drawData.materialParams.z;
    float emission = drawData.materialParams.w;

    if (isValidTexture(ormIdx)) {
        vec3 ormValues = unpackORM(textureGrad(bindlessTextures[nonuniformEXT(ormIdx)], texCoords, texDx, texDy));
        ao = ormValues.x;
        roughness = ormValues.y;
        metallic = ormValues.z;
    } else {
        if (isValidTexture(metallicIdx)) {
            metallic = textureGrad(bindlessTextures[nonuniformEXT(metallicIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(roughnessIdx)) {
            roughness = textureGrad(bindlessTextures[nonuniformEXT(roughnessIdx)], texCoords, texDx, texDy).r;
        }
        if (isValidTexture(aoIdx)) {
            ao = textureGrad(bindlessTextures[nonuniformEXT(aoIdx)], texCoords, texDx, texDy).r;
        }
    }

    if (isValidTexture(normalIdx)) {
        vec3 pos_dx = dFdx(fragWorldPos);
        vec3 pos_dy = dFdy(fragWorldPos);
        vec2 uv_dx = dFdx(fragTexCoord);
        vec2 uv_dy = dFdy(fragTexCoord);

        vec3 T = normalize(pos_dx * uv_dy.y - pos_dy * uv_dx.y);
        vec3 B = normalize(pos_dy * uv_dx.x - pos_dx * uv_dy.x);
        T = normalize(T - N * dot(N, T));
        B = cross(N, T);
        mat3 TBN = mat3(T, B, N);

        vec3 tangentNormal = textureGrad(bindlessTextures[nonuniformEXT(normalIdx)], texCoords, texDx, texDy).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo * drawData.iblDiffuse;

    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y) * drawData.iblSpecular;

    vec3 ambient = (kD * diffuse + specular) * ao;

    vec3 directLighting = vec3(0.0);
    float minShadow = 1.0;  // Track minimum shadow for ambient occlusion

    // Compute linear depth once for shadow cascade selection (also used for cluster lookup)
    float linearZ = linearizeDepth(gl_FragCoord.z);

    // Skip cluster lookup if no local lights exist (uniform branch, no divergence)
    if (lightCounts.pointCount > 0u || lightCounts.spotCount > 0u) {
        uint clusterIdx = getClusterIndex(gl_FragCoord.xy, linearZ);

        ClusterLightData clusterData = clusterLightGrid[clusterIdx];
        uint clusterPointCount = clusterData.counts & 0xFFFFu;
        uint clusterSpotCount = clusterData.counts >> 16u;
        uint lightOffset = clusterData.offset;

        // Point lights with shadow
        for (uint i = 0u; i < clusterPointCount; ++i) {
            uint lightIdx = lightIndexList[lightOffset + i];
            PointLight light = pointLights[lightIdx];
            float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, light.position, light.radius);
            minShadow = min(minShadow, shadow);
            directLighting += evaluatePointLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }

        // Spot lights with shadow
        for (uint i = 0u; i < clusterSpotCount; ++i) {
            uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
            uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
            SpotLight light = spotLights[lightIdx];
            float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos);
            minShadow = min(minShadow, shadow);
            directLighting += evaluateSpotLight(fragWorldPos, N, V, albedo, metallic, roughness, F0, light) * shadow;
        }
    }

    // Directional lights with shadow (CSM)
    for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
        DirectionalLight light = directionalLights[i];
        float shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, linearZ);
        minShadow = min(minShadow, shadow);
        directLighting += evaluateDirectionalLight(N, V, albedo, metallic, roughness, F0, light) * shadow;
    }

    // Apply shadow intensity to ambient (reduces ambient in shadowed areas)
    // shadowIntensity: 0 = no ambient occlusion, 1 = full ambient occlusion in shadows
    //
    // With PCF soft shadows, minShadow is often in 0.3-0.7 range instead of 0 or 1.
    // Apply a power curve to make soft shadows darker while preserving soft edges.
    // Higher shadowIntensity = more contrast = darker shadows
    float shadowContrast = 1.0 + lightCounts.shadowIntensity * 2.0;  // Range [1, 3]
    float adjustedShadow = pow(minShadow, shadowContrast);
    float ambientShadowFactor = mix(1.0, adjustedShadow, lightCounts.shadowIntensity);
    ambient *= ambientShadowFactor;

    float emissionMultiplier = emission;
    if (drawData.shaderGroupIndex == 2u) {
        emissionMultiplier *= cos(camera.time);
    }

    vec3 emissive = vec3(0.0);
    if (isValidTexture(emissionIdx)) {
        emissive = pow(textureGrad(bindlessTextures[nonuniformEXT(emissionIdx)], texCoords, texDx, texDy).rgb, vec3(2.2)) * emissionMultiplier;
    } else {
        emissive = albedo * emissionMultiplier;
    }

    vec3 color = ambient + directLighting + emissive;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

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
        uint lod = min(drawData.lodLevel, 3u);
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
        float linearZ = linearizeDepth(gl_FragCoord.z);
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

    // Depth visualization - heat map from near (blue) to far (red)
    if (viewModeValue == 5u) {
        float linearZ = linearizeDepth(gl_FragCoord.z);
        float near = clusterParams.depthParams.x;
        float far = clusterParams.depthParams.y;
        float normalizedDepth = clamp((linearZ - near) / (far - near), 0.0, 1.0);

        // Heat map: blue -> cyan -> green -> yellow -> red
        vec3 depthColors[5] = vec3[5](
            vec3(0.0, 0.0, 1.0),   // Blue (near)
            vec3(0.0, 1.0, 1.0),   // Cyan
            vec3(0.0, 1.0, 0.0),   // Green
            vec3(1.0, 1.0, 0.0),   // Yellow
            vec3(1.0, 0.0, 0.0)    // Red (far)
        );
        float t = normalizedDepth * 4.0;
        int idx = clamp(int(floor(t)), 0, 3);
        color = mix(depthColors[idx], depthColors[idx + 1], fract(t));
    }

    // Shadow visualization - shows combined shadow factor (white = lit, black = shadowed)
    if (viewModeValue == 6u) {
        float totalShadow = 1.0;

        // Directional light shadows (CSM)
        for (uint i = 0u; i < lightCounts.directionalCount; ++i) {
            DirectionalLight light = directionalLights[i];
            float shadow = sampleDirectionalShadow(light.shadowIndex, fragWorldPos, linearZ);
            totalShadow = min(totalShadow, shadow);
        }

        // Point and spot light shadows from cluster
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
                    float shadow = samplePointShadow(light.shadowIndex, fragWorldPos, light.position, light.radius);
                    totalShadow = min(totalShadow, shadow);
                }
            }

            for (uint i = 0u; i < clusterSpotCount; ++i) {
                uint packedIdx = lightIndexList[lightOffset + clusterPointCount + i];
                uint lightIdx = packedIdx & LIGHT_INDEX_MASK;
                SpotLight light = spotLights[lightIdx];
                if (light.shadowIndex >= 0) {
                    float shadow = sampleSpotShadow(light.shadowIndex, fragWorldPos);
                    totalShadow = min(totalShadow, shadow);
                }
            }
        }

        // Visualize shadow: purple = fully shadowed, white = fully lit
        vec3 shadowColor = mix(vec3(0.1, 0.1, 0.3), vec3(1.0, 0.95, 0.9), totalShadow);
        color = shadowColor;
    }

    outColor = vec4(color, alpha);
}
