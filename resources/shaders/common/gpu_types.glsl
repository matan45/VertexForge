#ifndef GPU_TYPES_GLSL
#define GPU_TYPES_GLSL

// Must match PerDrawData in GPUDrivenTypes.hpp (256 bytes)
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

    uint lodLevel;          // bits 0-7: LOD level (0-3), bits 8-15: crossfade alpha (0=visible, 255=fading)
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint boneMatrixOffset;
    uint instanceCount;     // Number of instances (1 = non-instanced)
    uint blendModeAndOpacity; // low 8 bits: BlendMode enum, bits 16-31: half-float opacity

    uvec4 instanceData; // .w=instanceOffset
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

// Must match GPUObjectData in GPUDrivenTypes.hpp (352 bytes)
struct GPUObjectData {
    mat4 modelMatrix;

    vec4 aabbMin;  // .w = maxDrawDistanceSquared (0 = use category default)
    vec4 aabbMax;  // .w = instanceCount (uint via floatBitsToUint, 0 or 1 = non-instanced)

    uvec4 lod0Data;
    uvec4 lod1Data;
    uvec4 lod2Data;
    uvec4 lod3Data;

    vec4 lodThresholds;

    vec4 albedo;
    vec4 materialParams;
    vec4 iblParams;

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

    uvec4 instanceData; // .w=instanceOffset
};

// Must match BatchDrawStats in GPUDrivenTypes.hpp (32 bytes)
struct BatchDrawStats {
    uint drawCount;
    uint lodCount0;
    uint lodCount1;
    uint lodCount2;
    uint lodCount3;
    uint culledByFrustum;
    uint culledByOcclusion;
    uint culledByDistance;
};

// VkDrawMeshTasksIndirectCommandEXT (12 bytes)
struct MeshTasksCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

// Must match TerrainTileGPUData in GPUDrivenTypes.hpp (256 bytes)
struct TerrainTileGPUData {
    mat4 modelMatrix;
    vec4 boundingSphere;        // xyz = world center, w = radius
    vec4 aabbMin;               // xyz = world AABB min, w = weightMapResolution (33/65/129)
    vec4 aabbMax;               // xyz = world AABB max, w = packed layerIndices[0-3] (floatBitsToUint)
    uvec4 lod0MeshletData;      // x = meshletOffset, y = meshletCount (total), z = baseVertexOffset, w = mainMeshletCount (surface only, no skirts)
    uvec4 lod1MeshletData;
    uvec4 lod2MeshletData;
    uvec4 lod3MeshletData;
    uvec4 lod4MeshletData;
    uvec4 lod5MeshletData;
    vec4 lodGeometricErrors;    // Per-LOD geometric error thresholds LOD 0-3 (world units)
    vec4 lodGeometricErrors2;   // x=LOD4 error, y=LOD5 error, z=packed layerIndices[4-7], w=unused
    int coordX;
    int coordZ;
    uint flags;
    uint weightMapOffset;       // Byte offset into weight map SSBO
};

// Must match TerrainLayerGPUData in GPUDrivenTypes.hpp (32 bytes)
struct TerrainLayerGPUData {
    uint albedoTextureIndex;    // Bindless index (0 = default white)
    uint normalTextureIndex;    // Bindless index (0 = default)
    float tilingScale;
    uint ormTextureIndex;       // Bindless index (0 = no ORM texture)
    float roughness;            // Scalar fallback when no ORM
    float metallic;             // Scalar fallback when no ORM
    float ao;                   // Scalar fallback when no ORM
    float emissionStrength;     // Emission intensity
};

uvec4 getTerrainLODMeshletData(TerrainTileGPUData tile, uint lodLevel) {
    if (lodLevel == 0) return tile.lod0MeshletData;
    if (lodLevel == 1) return tile.lod1MeshletData;
    if (lodLevel == 2) return tile.lod2MeshletData;
    if (lodLevel == 3) return tile.lod3MeshletData;
    if (lodLevel == 4) return tile.lod4MeshletData;
    return tile.lod5MeshletData;
}

#endif // GPU_TYPES_GLSL
