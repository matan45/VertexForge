#ifndef GPU_TYPES_GLSL
#define GPU_TYPES_GLSL

// Must match PerDrawData in GPUDrivenTypes.hpp (240 bytes)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint boneMatrixOffset;
    uint boneCount;
    uint padding3;
};

// Must match GPUMeshlet in MeshletBufferTypes.hpp (48 bytes)
struct GPUMeshlet {
    uint vertexOffset;
    uint primitiveOffset;
    uint vertexPrimCount;
    uint globalVertexOffset;
    vec4 boundingSphere;
    vec4 cone;
};

void unpackMeshletCounts(uint packed, out uint vertexCount, out uint primitiveCount) {
    vertexCount = packed & 0xFFu;
    primitiveCount = (packed >> 8) & 0xFFu;
}

// Must match GPUObjectData in GPUDrivenTypes.hpp (320 bytes)
struct GPUObjectData {
    mat4 modelMatrix;

    vec4 boundingSphere;

    uvec4 lod0Data;      // In DAG mode: dagHeaderIndex, clusterOffset, clusterCount, rootClusterIndex
    uvec4 lod1Data;      // In DAG mode: streamingUnitOffset, streamingUnitCount, streamingUnitMask, leafClusterCount
    uvec4 lod2Data;      // In DAG mode: maxDepth, reserved, reserved, reserved
    uvec4 lod3Data;      // In DAG mode: reserved

    vec4 lodThresholds;

    vec4 albedo;
    vec4 materialParams;
    vec4 iblParams;      // z = clusterErrorMultiplier

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint flags;
    uint entityId;
    uint availableLODMask;
    uint shaderGroupIndex;

    uvec4 meshletLod0;
    uvec4 meshletLod1;
    uvec4 meshletLod2;
    uvec4 meshletLod3;
};

// =====================================================================
// Object Flags - must match ObjectFlags in GPUDrivenTypes.hpp
// =====================================================================
#define FLAG_ALPHA_MASK       (1u << 4)
#define FLAG_UNIFORM_SCALE    (1u << 9)
#define FLAG_USE_CLUSTER_DAG  (1u << 10)
#define FLAG_DAG_FULLY_LOADED (1u << 11)

// =====================================================================
// DAG Field Accessors (when FLAG_USE_CLUSTER_DAG is set)
// =====================================================================

// lod0Data in DAG mode: cluster info
uint getDagHeaderIndex(GPUObjectData obj) { return obj.lod0Data.x; }
uint getDagClusterOffset(GPUObjectData obj) { return obj.lod0Data.y; }
uint getDagClusterCount(GPUObjectData obj) { return obj.lod0Data.z; }
uint getDagRootClusterIndex(GPUObjectData obj) { return obj.lod0Data.w; }

// lod1Data in DAG mode: streaming info
uint getDagStreamingUnitOffset(GPUObjectData obj) { return obj.lod1Data.x; }
uint getDagStreamingUnitCount(GPUObjectData obj) { return obj.lod1Data.y; }
uint getDagStreamingUnitMask(GPUObjectData obj) { return obj.lod1Data.z; }
uint getDagLeafClusterCount(GPUObjectData obj) { return obj.lod1Data.w; }

// lod2Data in DAG mode: depth info
uint getDagMaxDepth(GPUObjectData obj) { return obj.lod2Data.x; }

// iblParams.z = clusterErrorMultiplier (used by both modes)
float getClusterErrorMultiplier(GPUObjectData obj) { return obj.iblParams.z; }

// Mode check helpers
bool usesClusterDAG(GPUObjectData obj) {
    return (obj.flags & FLAG_USE_CLUSTER_DAG) != 0u;
}

bool isDAGFullyLoaded(GPUObjectData obj) {
    return (obj.flags & FLAG_DAG_FULLY_LOADED) != 0u;
}

// Must match BatchDrawStats in GPUDrivenTypes.hpp (32 bytes)
struct BatchDrawStats {
    uint drawCount;
    uint lodCount0;
    uint lodCount1;
    uint lodCount2;
    uint lodCount3;
    uint culledByFrustum;
    uint culledByOcclusion;
    uint padding;
};

// VkDrawMeshTasksIndirectCommandEXT (12 bytes)
struct MeshTasksCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

#endif // GPU_TYPES_GLSL
