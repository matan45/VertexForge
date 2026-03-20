#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"
#include "../common/hiz_occlusion.glsl"

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

layout(std430, set = 0, binding = 6) readonly buffer ActiveIndexBuffer {
    uint activeIndices[];
};

uvec4 getMeshletLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
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

// Compute crossfade alpha for LOD transition dithering.
// Returns 0-255 byte: 0 = fully visible, 255 = fully fading out.
// Transition zone is just below each LOD boundary — as screenPixels drops
// toward the boundary, crossfade increases (more dither). Once it crosses
// the boundary and a coarser LOD is selected, crossfade resets to 0.
const float LOD_CROSSFADE_FRACTION = 0.04; // 4% of threshold — narrow zone

uint computeCrossfadeByte(float screenPixels, vec4 thresholds, float globalBias, uint selectedLOD) {
    if (selectedLOD >= 3u) return 0u;

    float adjustedPixels = screenPixels * pow(2.0, -(thresholds.w + globalBias));

    // Get the boundary for the NEXT coarser LOD (the one we're approaching)
    float boundary;
    if (selectedLOD == 0u) boundary = thresholds.x;
    else if (selectedLOD == 1u) boundary = thresholds.y;
    else boundary = thresholds.z;

    float transitionWidth = boundary * LOD_CROSSFADE_FRACTION;

    // adjustedPixels is above boundary (we're at selectedLOD).
    // As it drops toward boundary, we fade out: alpha goes 0 -> 1.
    float distAboveBoundary = adjustedPixels - boundary;
    if (distAboveBoundary >= 0.0 && distAboveBoundary < transitionWidth) {
        float alpha = 1.0 - distAboveBoundary / transitionWidth; // 0 at top, 1 near boundary
        return uint(clamp(alpha, 0.0, 1.0) * 255.0);
    }
    return 0u;
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

// Wrapper for backward compatibility — delegates to shared hiz_occlusion.glsl
bool hiZOcclusionTest(vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    return hiZOcclusionTest(hiZTexture, worldSphere, viewProjection, screenSize, hiZMipLevels);
}

void main() {
    uint threadIndex = gl_GlobalInvocationID.x;
    if (threadIndex >= camera.objectCount) {
        return;
    }

    uint objectIndex = activeIndices[threadIndex];
    GPUObjectData obj = objects[objectIndex];

    uint batchIndex = objectIndex % camera.batchCount;
    uint shaderGroup = obj.shaderGroupIndex;
    uint sectionIndex = getSectionIndex(batchIndex, shaderGroup);
    uint commandsPerSection = getCommandsPerSection();

    uint instanceCount = floatBitsToUint(obj.aabbMax.w);
    if (instanceCount == 0u) instanceCount = 1u;
    bool isInstanced = instanceCount > 1u;

    vec3 worldAabbMin, worldAabbMax;
    transformAABB(obj.aabbMin.xyz, obj.aabbMax.xyz, obj.modelMatrix, worldAabbMin, worldAabbMax);

    vec3 worldCenter = (worldAabbMin + worldAabbMax) * 0.5;
    float worldRadius = length(worldAabbMax - worldCenter);
    vec4 worldSphere = vec4(worldCenter, worldRadius);

    // Skip group-level culling for instanced objects — the task shader
    // handles per-instance frustum, distance, and LOD culling individually.
    if (!isInstanced) {
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
    }

    uint targetLOD = 0;
    uint lodLevel;
    uvec4 meshletLodData;
    uint meshletOffset;
    uint meshletCount;
    uint baseVertexOffset;
    float screenPixelsCrossfade = 0.0;

    if (isInstanced) {
        // For instanced objects, the task shader handles per-instance LOD selection.
        // Here we just need LOD 0 (base) for the perDrawData fields; the task shader
        // will override meshletOffset/meshletCount per-instance.
        lodLevel = findBestAvailableLOD(0u, obj.availableLODMask);
        if (lodLevel == 0xFFFFFFFFu) {
            return;
        }
        meshletLodData = getMeshletLODData(obj, lodLevel);
        meshletOffset = meshletLodData.x;
        meshletCount = meshletLodData.y;
        baseVertexOffset = meshletLodData.z;
    } else {
        if (camera.enableLODSelection != LOD_SELECTION_DISABLED) {
            vec4 viewSphere = camera.view * vec4(worldSphere.xyz, 1.0);
            viewSphere.w = worldSphere.w;
            screenPixelsCrossfade = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
            targetLOD = selectLOD(screenPixelsCrossfade, obj.lodThresholds, camera.globalLodBias);
        }

        lodLevel = findBestAvailableLOD(targetLOD, obj.availableLODMask);
        if (lodLevel == 0xFFFFFFFFu) {
            return;
        }

        meshletLodData = getMeshletLODData(obj, lodLevel);
        meshletOffset = meshletLodData.x;
        meshletCount = meshletLodData.y;
        baseVertexOffset = meshletLodData.z;

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

    // For instanced objects, use max meshlet count across all available LODs
    // so each instance's task workgroup can independently select its own LOD.
    uint maxMeshletCount = meshletCount;
    if (isInstanced) {
        for (uint lod = 0u; lod < 4u; ++lod) {
            if ((obj.availableLODMask & (1u << lod)) != 0u) {
                uvec4 ld = getMeshletLODData(obj, lod);
                maxMeshletCount = max(maxMeshletCount, ld.y);
            }
        }
    }
    uint taskGroupCount = (maxMeshletCount + TASK_WORKGROUP_SIZE - 1u) / TASK_WORKGROUP_SIZE;

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
    // Pack LOD level (bits 0-7) and crossfade alpha (bits 8-15)
    uint packedLodLevel = lodLevel;
    if (camera.enableLODSelection == LOD_SELECTION_WITH_CROSSFADE && !isInstanced) {
        uint crossfadeByte = computeCrossfadeByte(screenPixelsCrossfade, obj.lodThresholds, camera.globalLodBias, lodLevel);
        packedLodLevel = lodLevel | (crossfadeByte << 8u);
    }
    perDrawData[globalDrawIndex].lodLevel = packedLodLevel;
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

    perDrawData[globalDrawIndex].instanceData = obj.instanceData;  // .w = instanceOffset
}
