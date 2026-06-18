#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

// Per-shadow-view GPU culling (Tier 4, UE5-style compacted shadow buffer).
//
// Unlike gpu_cull_lod.glsl (which culls against the MAIN camera into a section-structured
// buffer reused by every shadow page), this shader culls the scene against ONE shadow VIEW's
// frustum and appends the survivors as a FLAT list into a per-view region of a single shared
// shadow draw buffer. Each shadow view (directional clipmap level / spot / point cube face)
// gets its own region selected by push-constant `viewBase`; all pages of that view then issue
// a single drawMeshTasksIndirectCountEXT over the region (the shadow task shader does the fine
// per-page crop-frustum + dual-layer filtering). Shadows are depth-only / single-pipeline, so
// no per-shaderGroup sections are needed — that is what collapses the pages x sections draws.
//
// Reuses the GPUCullLODPipeline descriptor-set layout (set 0: object / camera / drawCmd /
// perDraw / drawCount / hiZ / activeIndex) so the external-descriptor-set machinery can bind
// the per-view camera + shared shadow output buffers. Occlusion culling is disabled via the
// per-view camera (the Hi-Z pyramid belongs to the main camera, not the light), binding 5 is
// left bound but unused.

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"
#include "../common/gpu_draw_functions.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform ShadowCullPushConstants {
    uint viewBase;     // first draw slot for this view (slot * SHADOW_DRAWS_PER_VIEW)
    uint capacity;     // SHADOW_DRAWS_PER_VIEW — max draws this view may emit
    uint countIndex;   // index of this view's counter in the shadow count buffer
    uint _pad;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;   // the SHADOW VIEW's camera (frustum = light view, occlusion off)
};

layout(std430, set = 0, binding = 2) writeonly buffer DrawCommandBuffer {
    MeshTasksCommand drawCommands[];
};

layout(std430, set = 0, binding = 3) writeonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Shadow count buffer: one atomic uint counter per view (NOT BatchDrawStats — flat layout).
layout(std430, set = 0, binding = 4) buffer ShadowCountBuffer {
    uint shadowDrawCounts[];
};

layout(set = 0, binding = 5) uniform sampler2D hiZTexture; // bound but unused (occlusion off)

layout(std430, set = 0, binding = 6) readonly buffer ActiveIndexBuffer {
    uint activeIndices[];
};

void main() {
    uint threadIndex = gl_GlobalInvocationID.x;
    if (threadIndex >= camera.objectCount) {
        return;
    }

    uint objectIndex = activeIndices[threadIndex];
    GPUObjectData obj = objects[objectIndex];

    // Translucent objects do not cast opaque shadows — skip them entirely.
    if ((obj.flags & FLAG_TRANSLUCENT) != 0u) {
        return;
    }

    uint instanceCount = floatBitsToUint(obj.aabbMax.w);
    if (instanceCount == 0u) instanceCount = 1u;
    bool isInstanced = instanceCount > 1u;

    vec3 worldAabbMin, worldAabbMax;
    transformAABB(obj.aabbMin.xyz, obj.aabbMax.xyz, obj.modelMatrix, worldAabbMin, worldAabbMax);

    // View-frustum culling against the shadow view (skip for instanced — the shadow task
    // shader frustum-culls each instance individually against the per-page crop matrix).
    if (!isInstanced && camera.enableFrustumCulling != 0u && (obj.flags & FLAG_NO_CULL) == 0u) {
        if (!aabbInFrustum(worldAabbMin, worldAabbMax, camera.frustumPlanes)) {
            return;
        }
    }

    // LOD: shadows use LOD 0 (base) — the per-view ortho/perspective projection makes camera-
    // relative screen-size LOD meaningless here, and shadow silhouettes want full geometry.
    uint lodLevel = findBestAvailableLOD(0u, obj.availableLODMask);
    if (lodLevel == 0xFFFFFFFFu) {
        return;
    }
    uvec4 meshletLodData = getMeshletLODData(obj, lodLevel);
    uint meshletOffset = meshletLodData.x;
    uint meshletCount  = meshletLodData.y;
    uint baseVertexOffset = meshletLodData.z;

    if (!isInstanced) {
        while (lodLevel > 0u && meshletCount == 0u) {
            lodLevel--;
            if ((obj.availableLODMask & (1u << lodLevel)) == 0u) continue;
            meshletLodData = getMeshletLODData(obj, lodLevel);
            meshletOffset = meshletLodData.x;
            meshletCount  = meshletLodData.y;
            baseVertexOffset = meshletLodData.z;
        }
        if (meshletCount == 0u) {
            return;
        }
    }

    // Append to this view's flat region.
    uint localDrawIndex = atomicAdd(shadowDrawCounts[pc.countIndex], 1u);
    if (localDrawIndex >= pc.capacity) {
        // Overflow — undo and drop. The CPU logs when a view's count saturates.
        atomicAdd(shadowDrawCounts[pc.countIndex], uint(-1));
        return;
    }
    uint globalDrawIndex = pc.viewBase + localDrawIndex;

    uint taskGroupCount = computeTaskGroupCount(obj, meshletCount, isInstanced);

    drawCommands[globalDrawIndex].groupCountX = taskGroupCount;
    drawCommands[globalDrawIndex].groupCountY = instanceCount;
    drawCommands[globalDrawIndex].groupCountZ = 1u;

    // Shadows are depth-only and use plain LOD (no crossfade dither).
    perDrawData[globalDrawIndex] = makePerDrawData(obj, objectIndex, lodLevel,
                                                   meshletOffset, meshletCount,
                                                   baseVertexOffset, instanceCount);
}
