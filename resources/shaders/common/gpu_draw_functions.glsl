#ifndef GPU_DRAW_FUNCTIONS_GLSL
#define GPU_DRAW_FUNCTIONS_GLSL

// Shared GPU-driven draw helpers for the main-camera cull compute shader
// (gpu_cull_lod.glsl). Single source of truth for the
// material flag bits, LOD selection, task-group sizing, and PerDrawData packing
// so the main-camera and per-shadow-view cull passes cannot silently desync.
//
// Note: makePerDrawData() returns PerDrawData BY VALUE on purpose — common
// includes are pulled in before the per-shader SSBO declarations, so a helper
// here cannot reference the perDrawData[] buffer global. Callers do:
//     perDrawData[i] = makePerDrawData(...);

#include "gpu_types.glsl"

// Material/draw flags — must match the bit layout in GPUDrivenTypes.hpp.
const uint FLAG_ALPHA_MASK     = 1u << 4;
const uint FLAG_TRANSLUCENT    = 1u << 5;
const uint FLAG_NO_CULL        = 1u << 6;
const uint FLAG_NO_OCCLUDE     = 1u << 7;
const uint FLAG_UNIFORM_SCALE  = 1u << 9;
const uint FLAG_ADDITIVE_BLEND = 1u << 10;
const uint FLAG_MULTIPLY_BLEND = 1u << 11;

const uint CATEGORY_SHIFT = 13u;
const uint CATEGORY_MASK  = 0xFu;

// VK-1415: per-object render-layer index (0-31) packed in flags bits 18-22.
// Matched against a camera's cullingMask in gpu_cull_lod.glsl. Must match ObjectFlags::Layer* in GPUDrivenTypes.hpp.
const uint LAYER_SHIFT = 18u;
const uint LAYER_MASK  = 0x1Fu;

// VK-1493: toon shading model (bits 23-24) + toon profile index (bits 25-31) are
// packed by ObjectFlags::packShadingFlags on the CPU and carried through
// makePerDrawData verbatim (d.flags = obj.flags). The cull pass never inspects them;
// the fragment shader unpacks them via SHADING_MODEL_SHIFT / PROFILE_INDEX_SHIFT in
// resources/shaders/common/toon_lighting.glsl.

// Task workgroup size — must match the mesh/task shaders that consume the draws.
const uint TASK_WORKGROUP_SIZE = 32u;

uvec4 getMeshletLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.meshletLod0;
        case 1: return obj.meshletLod1;
        case 2: return obj.meshletLod2;
        default: return obj.meshletLod3;
    }
}

// Returns the requested LOD if available, else the next-finer available LOD,
// else any available LOD; 0xFFFFFFFF when the object has no meshlet data at all.
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

// Number of task workgroups to dispatch for this draw. For instanced objects the
// task shader selects LOD per-instance, so size for the largest available LOD.
uint computeTaskGroupCount(GPUObjectData obj, uint meshletCount, bool isInstanced) {
    uint maxMeshletCount = meshletCount;
    if (isInstanced) {
        for (uint lod = 0u; lod < 4u; ++lod) {
            if ((obj.availableLODMask & (1u << lod)) != 0u) {
                uvec4 ld = getMeshletLODData(obj, lod);
                maxMeshletCount = max(maxMeshletCount, ld.y);
            }
        }
    }
    return (maxMeshletCount + TASK_WORKGROUP_SIZE - 1u) / TASK_WORKGROUP_SIZE;
}

// Single source of truth for the PerDrawData packing contract. The caller passes
// the already-resolved LOD/meshlet fields and the packed LOD level (plain lodLevel,
// or lodLevel | crossfadeByte<<8 for the main pass).
PerDrawData makePerDrawData(GPUObjectData obj, uint objectIndex, uint packedLodLevel,
                            uint meshletOffset, uint meshletCount,
                            uint baseVertexOffset, uint instanceCount) {
    PerDrawData d;

    d.modelMatrix = obj.modelMatrix;

    mat3 modelMat3 = mat3(obj.modelMatrix);
    mat3 normalMat3;
    if ((obj.flags & FLAG_UNIFORM_SCALE) != 0u) {
        float scale = length(modelMat3[0]);
        normalMat3 = modelMat3 * (1.0 / scale);
    } else {
        normalMat3 = transpose(inverse(modelMat3));
    }
    d.normalMatrix = mat4(normalMat3);

    d.albedo = obj.albedo;
    d.materialParams = obj.materialParams;
    d.textureIndices0 = obj.textureIndices0;
    d.textureIndices1 = obj.textureIndices1;
    d.objectIndex = objectIndex;
    d.flags = obj.flags;
    d.iblDiffuse = obj.iblParams.x;
    d.iblSpecular = obj.iblParams.y;
    d.lodLevel = packedLodLevel;
    d.shaderGroupIndex = obj.shaderGroupIndex;
    d.meshletOffset = meshletOffset;
    d.meshletCount = meshletCount;
    d.baseVertexOffset = baseVertexOffset;
    d.boneMatrixOffset = obj.meshletLod3.w;
    d.instanceCount = instanceCount;

    // blendModeAndOpacity: bits 0-7 = blend mode, bits 8-15 = alpha cutoff, bits 16-31 = opacity.
    // Shadow casters early-return on FLAG_TRANSLUCENT before this point, and the shadow
    // pass ignores the blend mode, so handling all modes here is safe for both callers.
    uint blendMode = 0u;
    if ((obj.flags & FLAG_ALPHA_MASK) != 0u) blendMode = 1u;
    if ((obj.flags & FLAG_TRANSLUCENT) != 0u) blendMode = 2u;
    if ((obj.flags & FLAG_ADDITIVE_BLEND) != 0u) blendMode = 3u;
    if ((obj.flags & FLAG_MULTIPLY_BLEND) != 0u) blendMode = 4u;
    uint alphaCutoffBits = uint(clamp(obj.iblParams.z, 0.0, 1.0) * 255.0);
    uint opacityBits = uint(clamp(obj.albedo.a, 0.0, 1.0) * 65535.0);
    d.blendModeAndOpacity = blendMode | (alphaCutoffBits << 8u) | (opacityBits << 16u);

    d.instanceData = obj.instanceData; // .w = instanceOffset

    return d;
}

#endif // GPU_DRAW_FUNCTIONS_GLSL
