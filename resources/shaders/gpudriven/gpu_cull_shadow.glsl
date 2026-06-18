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

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;

const uint FLAG_ALPHA_MASK     = 1u << 4;
const uint FLAG_TRANSLUCENT    = 1u << 5;
const uint FLAG_NO_CULL        = 1u << 6;
const uint FLAG_UNIFORM_SCALE  = 1u << 9;
const uint FLAG_ADDITIVE_BLEND = 1u << 10;
const uint FLAG_MULTIPLY_BLEND = 1u << 11;

const uint CATEGORY_SHIFT = 13u;
const uint CATEGORY_MASK  = 0xFu;

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

uvec4 getMeshletLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
}

uint findBestAvailableLOD(uint targetLOD, uint availableMask) {
    if (availableMask == 0xFu) return targetLOD;
    if (availableMask == 0u) return 0xFFFFFFFFu;
    for (uint lod = targetLOD; lod < 4u; ++lod)
        if ((availableMask & (1u << lod)) != 0u) return lod;
    for (uint lod = 0u; lod < 4u; ++lod)
        if ((availableMask & (1u << lod)) != 0u) return lod;
    return 0xFFFFFFFFu;
}

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
    {
        vec3 lo = obj.aabbMin.xyz;
        vec3 hi = obj.aabbMax.xyz;
        vec3 c[8];
        c[0] = (obj.modelMatrix * vec4(lo.x, lo.y, lo.z, 1.0)).xyz;
        c[1] = (obj.modelMatrix * vec4(hi.x, lo.y, lo.z, 1.0)).xyz;
        c[2] = (obj.modelMatrix * vec4(lo.x, hi.y, lo.z, 1.0)).xyz;
        c[3] = (obj.modelMatrix * vec4(hi.x, hi.y, lo.z, 1.0)).xyz;
        c[4] = (obj.modelMatrix * vec4(lo.x, lo.y, hi.z, 1.0)).xyz;
        c[5] = (obj.modelMatrix * vec4(hi.x, lo.y, hi.z, 1.0)).xyz;
        c[6] = (obj.modelMatrix * vec4(lo.x, hi.y, hi.z, 1.0)).xyz;
        c[7] = (obj.modelMatrix * vec4(hi.x, hi.y, hi.z, 1.0)).xyz;
        worldAabbMin = c[0];
        worldAabbMax = c[0];
        for (int i = 1; i < 8; ++i) { worldAabbMin = min(worldAabbMin, c[i]); worldAabbMax = max(worldAabbMax, c[i]); }
    }

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
    perDrawData[globalDrawIndex].lodLevel = lodLevel;
    perDrawData[globalDrawIndex].shaderGroupIndex = obj.shaderGroupIndex;
    perDrawData[globalDrawIndex].meshletOffset = meshletOffset;
    perDrawData[globalDrawIndex].meshletCount = meshletCount;
    perDrawData[globalDrawIndex].baseVertexOffset = baseVertexOffset;
    perDrawData[globalDrawIndex].boneMatrixOffset = obj.meshletLod3.w;
    perDrawData[globalDrawIndex].instanceCount = instanceCount;

    uint blendMode = 0u;
    if ((obj.flags & FLAG_ALPHA_MASK) != 0u) blendMode = 1u;
    uint alphaCutoffBits = uint(clamp(obj.iblParams.z, 0.0, 1.0) * 255.0);
    uint opacityBits = uint(clamp(obj.albedo.a, 0.0, 1.0) * 65535.0);
    perDrawData[globalDrawIndex].blendModeAndOpacity = blendMode | (alphaCutoffBits << 8u) | (opacityBits << 16u);

    perDrawData[globalDrawIndex].instanceData = obj.instanceData;
}
