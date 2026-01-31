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

// Must match GPUObjectData in GPUDrivenTypes.hpp (336 bytes)
struct GPUObjectData {
    mat4 modelMatrix;

    vec4 aabbMin;  // .w unused (padding)
    vec4 aabbMax;  // .w unused (padding)

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
    uint padding;
};

// VkDrawMeshTasksIndirectCommandEXT (12 bytes)
struct MeshTasksCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

#endif // GPU_TYPES_GLSL
