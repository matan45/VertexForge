#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"
#include "../common/gpu_draw_functions.glsl"
#include "../common/hiz_occlusion.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

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

    // VK-1415: per-object render layer vs per-camera culling mask. Single early-out, before any
    // other cull work, so a camera (incl. an RTT camera) renders only its selected layers.
    uint objLayerBit = 1u << ((obj.flags >> LAYER_SHIFT) & LAYER_MASK);
    if ((objLayerBit & camera.cullExtra.x) == 0u) {
        return;
    }

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

    // For instanced objects, size for the largest available LOD so each instance's
    // task workgroup can independently select its own LOD.
    uint taskGroupCount = computeTaskGroupCount(obj, meshletCount, isInstanced);

    drawCommands[globalDrawIndex].groupCountX = taskGroupCount;
    drawCommands[globalDrawIndex].groupCountY = instanceCount;
    drawCommands[globalDrawIndex].groupCountZ = 1u;

    // Pack LOD level (bits 0-7) and crossfade alpha (bits 8-15)
    uint packedLodLevel = lodLevel;
    if (camera.enableLODSelection == LOD_SELECTION_WITH_CROSSFADE && !isInstanced) {
        uint crossfadeByte = computeCrossfadeByte(screenPixelsCrossfade, obj.lodThresholds, camera.globalLodBias, lodLevel);
        packedLodLevel = lodLevel | (crossfadeByte << 8u);
    }

    perDrawData[globalDrawIndex] = makePerDrawData(obj, objectIndex, packedLodLevel,
                                                   meshletOffset, meshletCount,
                                                   baseVertexOffset, instanceCount);
}
