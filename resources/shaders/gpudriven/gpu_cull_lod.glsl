#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;

const uint FLAG_ALPHA_MASK     = 1u << 4;
const uint FLAG_TRANSLUCENT   = 1u << 5;
const uint FLAG_NO_CULL       = 1u << 6;
const uint FLAG_NO_OCCLUDE    = 1u << 7;
const uint FLAG_UNIFORM_SCALE = 1u << 9;
const uint FLAG_ADDITIVE_BLEND = 1u << 10;
const uint FLAG_MULTIPLY_BLEND = 1u << 11;

const uint CATEGORY_SHIFT = 13u;
const uint CATEGORY_MASK  = 0xFu;

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;
};

uint getCommandsPerSection() {
    return camera.commandsPerBatch / camera.shaderGroupCount;
}

uint getSectionIndex(uint batch, uint shaderGroup) {
    return batch * camera.shaderGroupCount + shaderGroup;
}

layout(std430, set = 0, binding = 2) writeonly buffer DrawCommandBuffer {
    MeshTasksCommand drawCommands[];
};

layout(std430, set = 0, binding = 3) writeonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 0, binding = 4) buffer DrawCountBuffer {
    BatchDrawStats batchStats[];
};

layout(set = 0, binding = 5) uniform sampler2D hiZTexture;

uvec4 getMeshletLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
}

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

void transformAABB(vec3 localMin, vec3 localMax, mat4 modelMatrix, out vec3 worldMin, out vec3 worldMax) {
    vec3 corners[8];
    corners[0] = (modelMatrix * vec4(localMin.x, localMin.y, localMin.z, 1.0)).xyz;
    corners[1] = (modelMatrix * vec4(localMax.x, localMin.y, localMin.z, 1.0)).xyz;
    corners[2] = (modelMatrix * vec4(localMin.x, localMax.y, localMin.z, 1.0)).xyz;
    corners[3] = (modelMatrix * vec4(localMax.x, localMax.y, localMin.z, 1.0)).xyz;
    corners[4] = (modelMatrix * vec4(localMin.x, localMin.y, localMax.z, 1.0)).xyz;
    corners[5] = (modelMatrix * vec4(localMax.x, localMin.y, localMax.z, 1.0)).xyz;
    corners[6] = (modelMatrix * vec4(localMin.x, localMax.y, localMax.z, 1.0)).xyz;
    corners[7] = (modelMatrix * vec4(localMax.x, localMax.y, localMax.z, 1.0)).xyz;

    worldMin = corners[0];
    worldMax = corners[0];
    for (int i = 1; i < 8; i++) {
        worldMin = min(worldMin, corners[i]);
        worldMax = max(worldMax, corners[i]);
    }
}

// AABB frustum test using p-vertex method
bool aabbInFrustum(vec3 aabbMin, vec3 aabbMax, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        vec3 planeNormal = frustumPlanes[i].xyz;
        float planeD = frustumPlanes[i].w;

        vec3 pVertex;
        pVertex.x = (planeNormal.x >= 0.0) ? aabbMax.x : aabbMin.x;
        pVertex.y = (planeNormal.y >= 0.0) ? aabbMax.y : aabbMin.y;
        pVertex.z = (planeNormal.z >= 0.0) ? aabbMax.z : aabbMin.z;

        float distance = dot(planeNormal, pVertex) + planeD;

        if (distance < 0.0) {
            return false;
        }
    }
    return true;
}

float projectSphereToScreen(vec4 worldSphere, mat4 projection, vec2 screenSize) {
    vec4 clipPos = projection * vec4(worldSphere.xyz, 1.0);
    if (clipPos.w <= 0.01) {
        return 10000.0;
    }
    float projectedRadius = worldSphere.w * abs(projection[1][1]) / clipPos.w;
    float screenDiameter = projectedRadius * screenSize.y;
    return screenDiameter;
}

uint selectLOD(float screenPixels, vec4 thresholds, float globalBias) {
    float adjustedPixels = screenPixels * pow(2.0, -(thresholds.w + globalBias));
    if (adjustedPixels > thresholds.x) return 0;
    if (adjustedPixels > thresholds.y) return 1;
    if (adjustedPixels > thresholds.z) return 2;
    return 3;
}

uint findBestAvailableLOD(uint targetLOD, uint availableMask) {
    if (availableMask == 0xFu) {
        return targetLOD;
    }
    if (availableMask == 0u) {
        return 0xFFFFFFFFu;
    }
    for (uint lod = targetLOD; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }
    for (uint lod = 0u; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }
    return 0xFFFFFFFFu;
}

bool hiZOcclusionTest(vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 center = worldSphere.xyz;
    float radius = worldSphere.w;
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        if (corners[i].w <= 0.0) {
            return true;
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    if (minDepth < 0.0) {
        return true;
    }

    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMax.x, uvMin.y), mipLevel).r);

    return minDepth <= hiZDepth + 0.0001;
}

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;
    if (objectIndex >= camera.objectCount) {
        return;
    }

    GPUObjectData obj = objects[objectIndex];

    uint batchIndex = objectIndex % camera.batchCount;
    uint shaderGroup = obj.shaderGroupIndex;
    uint sectionIndex = getSectionIndex(batchIndex, shaderGroup);
    uint commandsPerSection = getCommandsPerSection();

    vec3 worldAabbMin, worldAabbMax;
    transformAABB(obj.aabbMin.xyz, obj.aabbMax.xyz, obj.modelMatrix, worldAabbMin, worldAabbMax);

    vec3 worldCenter = (worldAabbMin + worldAabbMax) * 0.5;
    float worldRadius = length(worldAabbMax - worldCenter);
    vec4 worldSphere = vec4(worldCenter, worldRadius);

    // Distance culling - cheap squared-distance check before frustum/occlusion
    if (camera.enableDistanceCulling != 0u) {
        vec3 diff = worldCenter - camera.cameraPosition.xyz;
        float distSq = dot(diff, diff);

        // Per-object override stored in aabbMin.w (0 = use category default)
        float maxDistSq = obj.aabbMin.w;
        if (maxDistSq <= 0.0) {
            uint cat = (obj.flags >> CATEGORY_SHIFT) & CATEGORY_MASK;
            maxDistSq = (cat < 4u)
                ? camera.categoryDistSq0[cat]
                : camera.categoryDistSq1[cat - 4u];
        }

        if (maxDistSq > 0.0 && distSq > maxDistSq) {
            atomicAdd(batchStats[sectionIndex].culledByDistance, 1);
            return;
        }
    }

    if (camera.enableFrustumCulling != 0u && (obj.flags & FLAG_NO_CULL) == 0u) {
        if (!aabbInFrustum(worldAabbMin, worldAabbMax, camera.frustumPlanes)) {
            atomicAdd(batchStats[sectionIndex].culledByFrustum, 1);
            return;
        }
    }

    bool isTransparent = (obj.flags & FLAG_TRANSLUCENT) != 0u;
    if (camera.enableOcclusionCulling != 0u && (obj.flags & FLAG_NO_OCCLUDE) == 0u && !isTransparent) {
        if (camera.hiZMipLevels > 0u) {
            if (!hiZOcclusionTest(worldSphere, camera.viewProjection, camera.screenParams.xy, camera.hiZMipLevels)) {
                atomicAdd(batchStats[sectionIndex].culledByOcclusion, 1);
                return;
            }
        }
    }

    uint targetLOD = 0;
    if (camera.enableLODSelection != 0u) {
        vec4 viewSphere = camera.view * vec4(worldSphere.xyz, 1.0);
        viewSphere.w = worldSphere.w;
        float screenPixels = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
        targetLOD = selectLOD(screenPixels, obj.lodThresholds, camera.globalLodBias);
    }

    uint lodLevel = findBestAvailableLOD(targetLOD, obj.availableLODMask);
    if (lodLevel == 0xFFFFFFFFu) {
        return;
    }

    uvec4 meshletLodData = getMeshletLODData(obj, lodLevel);
    uint meshletOffset = meshletLodData.x;
    uint meshletCount = meshletLodData.y;
    uint baseVertexOffset = meshletLodData.z;

    while (lodLevel > 0u && meshletCount == 0u) {
        lodLevel--;
        if ((obj.availableLODMask & (1u << lodLevel)) == 0u) {
            continue;
        }
        meshletLodData = getMeshletLODData(obj, lodLevel);
        meshletOffset = meshletLodData.x;
        meshletCount = meshletLodData.y;
        baseVertexOffset = meshletLodData.z;
    }

    if (meshletCount == 0u) {
        return;
    }

    uint localDrawIndex = atomicAdd(batchStats[sectionIndex].drawCount, 1);
    if (localDrawIndex >= commandsPerSection) {
        atomicAdd(batchStats[sectionIndex].drawCount, uint(-1));
        return;
    }

    switch (lodLevel) {
        case 0u: atomicAdd(batchStats[sectionIndex].lodCount0, 1); break;
        case 1u: atomicAdd(batchStats[sectionIndex].lodCount1, 1); break;
        case 2u: atomicAdd(batchStats[sectionIndex].lodCount2, 1); break;
        default: atomicAdd(batchStats[sectionIndex].lodCount3, 1); break;
    }

    uint globalDrawIndex = sectionIndex * commandsPerSection + localDrawIndex;
    uint taskGroupCount = (meshletCount + TASK_WORKGROUP_SIZE - 1u) / TASK_WORKGROUP_SIZE;

    uint instanceCount = floatBitsToUint(obj.aabbMax.w);
    if (instanceCount == 0u) instanceCount = 1u;

    drawCommands[globalDrawIndex].groupCountX = taskGroupCount;
    drawCommands[globalDrawIndex].groupCountY = instanceCount;
    drawCommands[globalDrawIndex].groupCountZ = 1u;

    perDrawData[globalDrawIndex].modelMatrix = obj.modelMatrix;

    mat3 modelMat3 = mat3(obj.modelMatrix);
    mat3 normalMat3;
    if ((obj.flags & FLAG_UNIFORM_SCALE) != 0u) {
        float scale = length(modelMat3[0]);
        normalMat3 = modelMat3 * (1.0 / scale);
    } else {
        normalMat3 = transpose(inverse(modelMat3));
    }
    perDrawData[globalDrawIndex].normalMatrix = mat4(normalMat3);

    perDrawData[globalDrawIndex].albedo = obj.albedo;
    perDrawData[globalDrawIndex].materialParams = obj.materialParams;
    perDrawData[globalDrawIndex].textureIndices0 = obj.textureIndices0;
    perDrawData[globalDrawIndex].textureIndices1 = obj.textureIndices1;
    perDrawData[globalDrawIndex].objectIndex = objectIndex;
    perDrawData[globalDrawIndex].flags = obj.flags;
    perDrawData[globalDrawIndex].iblDiffuse = obj.iblParams.x;
    perDrawData[globalDrawIndex].iblSpecular = obj.iblParams.y;
    perDrawData[globalDrawIndex].lodLevel = lodLevel;
    perDrawData[globalDrawIndex].shaderGroupIndex = obj.shaderGroupIndex;
    perDrawData[globalDrawIndex].meshletOffset = meshletOffset;
    perDrawData[globalDrawIndex].meshletCount = meshletCount;
    perDrawData[globalDrawIndex].baseVertexOffset = baseVertexOffset;
    perDrawData[globalDrawIndex].boneMatrixOffset = obj.meshletLod3.w;
    perDrawData[globalDrawIndex].instanceCount = instanceCount;

    // blendModeAndOpacity: bits 0-7 = blend mode, bits 8-15 = alpha cutoff, bits 16-31 = opacity
    uint blendMode = 0u;
    if ((obj.flags & FLAG_ALPHA_MASK) != 0u) blendMode = 1u;
    if ((obj.flags & FLAG_TRANSLUCENT) != 0u) blendMode = 2u;
    if ((obj.flags & FLAG_ADDITIVE_BLEND) != 0u) blendMode = 3u;
    if ((obj.flags & FLAG_MULTIPLY_BLEND) != 0u) blendMode = 4u;
    uint alphaCutoffBits = uint(clamp(obj.iblParams.z, 0.0, 1.0) * 255.0);
    uint opacityBits = uint(clamp(obj.albedo.a, 0.0, 1.0) * 65535.0);
    perDrawData[globalDrawIndex].blendModeAndOpacity = blendMode | (alphaCutoffBits << 8u) | (opacityBits << 16u);

    perDrawData[globalDrawIndex].lightmapData = obj.lightmapData;  // .w = instanceOffset
}
