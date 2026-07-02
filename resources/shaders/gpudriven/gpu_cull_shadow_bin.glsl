#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

// VK-1479 Phase B1: page-binned directional shadow cull.
//
// One invocation per (active object, clipmap level). Frustum/distance/LOD-culls the caster
// against THAT level's light view-projection, projects its AABB to the overlapped virtual-shadow
// page range (shared shadow_page_overlap.glsl math), and appends a MeshTasksCommand + PerDrawData
// into the fixed-capacity per-page bin for each overlapped rendering page. Mirrors gpu_cull_lod.glsl
// closely so the shadow cull cannot silently desync from the main cull.

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"
#include "../common/gpu_draw_functions.glsl"
#include "../common/shadow_page_overlap.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Mirror of ShadowLevelData in ShadowPageBinner.hpp (96 bytes, std430).
struct ShadowLevelData {
    mat4 viewProjection;
    vec4 bias;   // depthBias, slopeBias, normalBias, unused
    vec4 params; // levelExtent, unused, unused, unused
};

// set 0 — must match ShadowPageBinner::createComputePipeline()'s descriptor set layout.
layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;
};

layout(std430, set = 0, binding = 2) readonly buffer ActiveIndexBuffer {
    uint activeIndices[];
};

layout(std430, set = 0, binding = 3) readonly buffer LevelBuffer {
    ShadowLevelData levels[];
};

layout(std430, set = 0, binding = 4) readonly buffer PageBinBaseBuffer {
    uint pageBinBase[];
};

layout(std430, set = 0, binding = 5) writeonly buffer BinCommandBuffer {
    MeshTasksCommand binCommands[];
};

layout(std430, set = 0, binding = 6) writeonly buffer BinPerDrawBuffer {
    PerDrawData binPerDraw[];
};

layout(std430, set = 0, binding = 7) buffer BinCountBuffer {
    uint binCount[];
};

layout(std430, set = 0, binding = 8) buffer OverflowBuffer {
    uint overflow[];
};

layout(push_constant) uniform PushConstants {
    uint objectCount;
    uint levelCount;
    uint pagesPerLevel;
    uint binCapacity;
    uint flags;       // bit0 distanceCull, bit1 occlusionCull (reserved/off in B1), bit2 lodEnabled
    uint pad0;
    uint pad1;
    uint pad2;
} pc;

// Local copies of gpu_cull_lod.glsl's projectSphereToScreen / selectLOD — shadows use them
// identically. Keep these statement-for-statement in sync with gpu_cull_lod.glsl.
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
    if (adjustedPixels > thresholds.x) return 0u;
    if (adjustedPixels > thresholds.y) return 1u;
    if (adjustedPixels > thresholds.z) return 2u;
    return 3u;
}

void main() {
    uint tid = gl_GlobalInvocationID.x;
    if (tid >= pc.objectCount) {
        return;
    }
    uint level = gl_GlobalInvocationID.y;
    if (level >= pc.levelCount) {
        return;
    }

    uint objectIndex = activeIndices[tid];
    GPUObjectData obj = objects[objectIndex];

    // Non shadow casters: blended geometry does not cast an opaque shadow.
    if ((obj.flags & (FLAG_TRANSLUCENT | FLAG_ADDITIVE_BLEND | FLAG_MULTIPLY_BLEND)) != 0u) {
        return;
    }

    uint instanceCount = floatBitsToUint(obj.aabbMax.w);
    if (instanceCount == 0u) instanceCount = 1u;
    bool isInstanced = instanceCount > 1u;

    vec3 worldAabbMin, worldAabbMax;
    transformAABB(obj.aabbMin.xyz, obj.aabbMax.xyz, obj.modelMatrix, worldAabbMin, worldAabbMax);
    vec3 worldCenter = (worldAabbMin + worldAabbMax) * 0.5;
    float worldRadius = length(worldAabbMax - worldCenter);

    mat4 lvp = levels[level].viewProjection;

    if (!isInstanced) {
        vec4 planes[6];
        extractFrustumPlanesFromVP(lvp, planes);
        if (!aabbInFrustum(worldAabbMin, worldAabbMax, planes)) {
            return;
        }

        if ((pc.flags & 1u) != 0u) {
            vec3 diff = worldCenter - camera.cameraPosition.xyz;
            float distSq = dot(diff, diff);

            // Per-object override stored in aabbMin.w (0 = use category default).
            float maxDistSq = obj.aabbMin.w;
            if (maxDistSq <= 0.0) {
                uint cat = (obj.flags >> CATEGORY_SHIFT) & CATEGORY_MASK;
                maxDistSq = (cat < 4u)
                    ? camera.categoryDistSq0[cat]
                    : camera.categoryDistSq1[cat - 4u];
            }
            if (maxDistSq > 0.0 && distSq > maxDistSq) {
                return;
            }
        }
        // Shadow-space occlusion culling is reserved for B3 (flags bit1); off in B1.
    }

    // LOD selection (main-style; NOT forced to LOD 0).
    uint lodLevel;
    uint meshletOffset;
    uint meshletCount;
    uint baseVertexOffset;

    if (isInstanced) {
        // Instanced draws select LOD per-instance in the task shader; base on LOD 0 here.
        lodLevel = findBestAvailableLOD(0u, obj.availableLODMask);
        if (lodLevel == 0xFFFFFFFFu) {
            return;
        }
        uvec4 ld = getMeshletLODData(obj, lodLevel);
        meshletOffset = ld.x;
        meshletCount = ld.y;
        baseVertexOffset = ld.z;
    } else {
        uint targetLOD = 0u;
        if ((pc.flags & 4u) != 0u) {
            vec4 viewSphere = camera.view * vec4(worldCenter, 1.0);
            viewSphere.w = worldRadius;
            float screenPixels = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
            targetLOD = selectLOD(screenPixels, obj.lodThresholds, camera.globalLodBias);
        }

        lodLevel = findBestAvailableLOD(targetLOD, obj.availableLODMask);
        if (lodLevel == 0xFFFFFFFFu) {
            return;
        }
        uvec4 ld = getMeshletLODData(obj, lodLevel);
        meshletOffset = ld.x;
        meshletCount = ld.y;
        baseVertexOffset = ld.z;

        while (lodLevel > 0u && meshletCount == 0u) {
            lodLevel--;
            if ((obj.availableLODMask & (1u << lodLevel)) == 0u) {
                continue;
            }
            ld = getMeshletLODData(obj, lodLevel);
            meshletOffset = ld.x;
            meshletCount = ld.y;
            baseVertexOffset = ld.z;
        }
        if (meshletCount == 0u) {
            return;
        }
    }

    uint taskGroupCount = computeTaskGroupCount(obj, meshletCount, isInstanced);
    PerDrawData draw = makePerDrawData(obj, objectIndex, lodLevel,
                                       meshletOffset, meshletCount,
                                       baseVertexOffset, instanceCount);

    // Overlapped page range within this level's page grid.
    PageRange range;
    if (isInstanced) {
        // Conservative: an instanced object's instances may land anywhere in the level. B3 tightens
        // this via a per-instance path; here we bin into every page of the level.
        range.fx0 = 0u;
        range.fy0 = 0u;
        range.fx1 = pc.pagesPerLevel - 1u;
        range.fy1 = pc.pagesPerLevel - 1u;
        range.valid = true;
    } else {
        range = overlappedPages(obj.aabbMin.xyz, obj.aabbMax.xyz, lvp * obj.modelMatrix,
                                pc.pagesPerLevel, pc.pagesPerLevel);
    }
    if (!range.valid) {
        return;
    }

    for (uint fy = range.fy0; fy <= range.fy1; ++fy) {
        for (uint fx = range.fx0; fx <= range.fx1; ++fx) {
            // Matches ShadowSystemFeedback's page-grid linearization: (level*ppl + fy)*ppl + fx.
            uint pageLinear = (level * pc.pagesPerLevel + fy) * pc.pagesPerLevel + fx;
            uint renderSlot = pageBinBase[pageLinear];
            if (renderSlot == 0xFFFFFFFFu) {
                continue; // page not rendering this frame (or overflowed the arena -> legacy fallback)
            }

            uint local = atomicAdd(binCount[renderSlot], 1u);
            if (local >= pc.binCapacity) {
                // Bin full: undo the count bump and record the overflow (diagnosable, never silent).
                atomicAdd(binCount[renderSlot], uint(-1));
                atomicAdd(overflow[0], 1u);
                atomicAdd(overflow[1], 1u);
                continue;
            }

            uint slot = renderSlot * pc.binCapacity + local;
            binCommands[slot].groupCountX = taskGroupCount;
            binCommands[slot].groupCountY = instanceCount;
            binCommands[slot].groupCountZ = 1u;
            binPerDraw[slot] = draw;
        }
    }
}
