#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <string>
#include "MeshletBufferTypes.hpp"

namespace render::gpudriven
{
    enum class LODStreamState : uint8_t
    {
        NotRequested,
        Queued,
        Streaming,
        Uploading,
        Ready
    };

    constexpr uint32_t MAX_GPU_OBJECTS = 65536;
    constexpr uint32_t MAX_DRAW_COMMANDS = 700000;
    constexpr uint32_t DEFAULT_BATCH_COUNT = 4;
    constexpr uint32_t MAX_BATCH_COUNT = 8;
    constexpr uint32_t MAX_BINDLESS_TEXTURES = 4096;
    constexpr uint32_t MAX_SHADER_GROUPS = 16;
    constexpr uint32_t LOD_LEVEL_COUNT = 4;
    constexpr uint32_t CULL_WORKGROUP_SIZE = 64;
    constexpr uint32_t INVALID_TEXTURE_INDEX = 0xFFFFFFFF;

    constexpr uint32_t MAX_BONES_PER_OBJECT = 128;
    constexpr uint32_t MAX_ANIMATED_OBJECTS = 1024;
    constexpr uint32_t INVALID_BONE_OFFSET = 0xFFFFFFFF;

    constexpr float LOD_THRESHOLD_0 = 400.0f;
    constexpr float LOD_THRESHOLD_1 = 200.0f;
    constexpr float LOD_THRESHOLD_2 = 100.0f;

    struct alignas(16) LODDrawInfo
    {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t vertexCount;
    };

    struct alignas(16) GPUObjectData
    {
        glm::mat4 modelMatrix;
        glm::vec4 aabbMin;  // .w unused (padding)
        glm::vec4 aabbMax;  // .w unused (padding)
        glm::uvec4 lod0Data;
        glm::uvec4 lod1Data;
        glm::uvec4 lod2Data;
        glm::uvec4 lod3Data;
        glm::vec4 lodThresholds;
        glm::vec4 albedo;
        glm::vec4 materialParams;
        glm::vec4 iblParams;
        glm::uvec4 textureIndices0;
        glm::uvec4 textureIndices1;
        uint32_t flags;
        uint32_t entityId;
        uint32_t availableLODMask;
        uint32_t shaderGroupIndex;
        glm::uvec4 meshletLod0;
        glm::uvec4 meshletLod1;
        glm::uvec4 meshletLod2;
        glm::uvec4 meshletLod3;  // .w = boneMatrixOffset
    };
    static_assert(sizeof(GPUObjectData) == 336);

    namespace ObjectFlags
    {
        constexpr uint32_t AlphaMask = 1 << 4;
        constexpr uint32_t Translucent = 1 << 5;
        constexpr uint32_t NoCull = 1 << 6;       // matches GPU shader FLAG_NO_CULL
        constexpr uint32_t NoOcclude = 1 << 7;    // matches GPU shader FLAG_NO_OCCLUDE
        constexpr uint32_t UniformScale = 1 << 9;
        constexpr uint32_t AdditiveBlend = 1 << 10;
        constexpr uint32_t MultiplyBlend = 1 << 11;
        constexpr uint32_t TerrainTile = 1 << 12;
    }

    struct alignas(16) TerrainTileGPUData
    {
        glm::mat4 modelMatrix;          // Usually identity for world-space terrain
        glm::vec4 boundingSphere;       // xyz = world center, w = radius
        glm::vec4 aabbMin;              // xyz = world AABB min, w = weightMapResolution (33/65/129)
        glm::vec4 aabbMax;              // xyz = world AABB max, w = activeLayerCount (1-16)
        glm::uvec4 lod0MeshletData;     // x = meshletOffset, y = meshletCount (total), z = baseVertexOffset, w = mainMeshletCount (surface only, no skirts)
        glm::uvec4 lod1MeshletData;     // Same layout
        glm::uvec4 lod2MeshletData;     // Same layout
        glm::uvec4 lod3MeshletData;     // Same layout
        glm::vec4 lodGeometricErrors;   // Per-LOD geometric error thresholds (world units)
        int32_t coordX;
        int32_t coordZ;
        uint32_t flags;
        uint32_t weightMapOffset;       // Byte offset into weight map SSBO
    };
    static_assert(sizeof(TerrainTileGPUData) == 208);

    // Per-layer texture indices for terrain material layers (16 bytes per layer)
    struct TerrainLayerGPUData
    {
        uint32_t albedoTextureIndex;   // Bindless index (0 = default white)
        uint32_t normalTextureIndex;   // Bindless index (0 = default)
        float tilingScale;             // UV tiling multiplier
        uint32_t padding;
    };
    static_assert(sizeof(TerrainLayerGPUData) == 16);

    struct alignas(16) TerrainCullingStats
    {
        uint32_t totalTiles;
        uint32_t culledTiles;
        uint32_t totalMeshlets;
        uint32_t culledMeshlets;
        uint32_t visibleMeshlets;
        uint32_t lodCount0;
        uint32_t lodCount1;
        uint32_t lodCount2;
        uint32_t lodCount3;
        uint32_t padding[3];
    };
    static_assert(sizeof(TerrainCullingStats) == 48);

    struct alignas(16) PerDrawData
    {
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
        glm::vec4 albedo;
        glm::vec4 materialParams;
        glm::uvec4 textureIndices0;
        glm::uvec4 textureIndices1;
        uint32_t objectIndex;
        uint32_t flags;
        float iblDiffuse;
        float iblSpecular;
        uint32_t lodLevel;
        uint32_t shaderGroupIndex;
        uint32_t meshletOffset;
        uint32_t meshletCount;
        uint32_t baseVertexOffset;
        uint32_t boneMatrixOffset;
        uint32_t boneCount;
        uint32_t blendModeAndOpacity; // low 8 bits: BlendMode enum, bits 16-31: half-float opacity
    };
    static_assert(sizeof(PerDrawData) == 240);

    struct alignas(16) GPUCameraData
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::mat4 invViewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 screenParams;
        glm::vec4 frustumPlanes[6];
        float farPlane;
        uint32_t objectCount;
        uint32_t hiZMipLevels;
        uint32_t frameIndex;
        uint32_t enableFrustumCulling;
        uint32_t enableOcclusionCulling;
        uint32_t enableLODSelection;
        uint32_t batchCount;
        uint32_t commandsPerBatch;
        uint32_t shaderGroupCount;
        uint32_t padding1;
        uint32_t padding2;
    };
    static_assert(sizeof(GPUCameraData) == 432);

    struct SubmeshLocation
    {
        std::string meshPath;
        std::string submeshName;
        uint32_t submeshIndex;
        std::array<LODDrawInfo, LOD_LEVEL_COUNT> lods;
        std::array<MeshletLODInfo, LOD_LEVEL_COUNT> meshletLods{};
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
        glm::vec4 boundingSphere;
        std::array<LODStreamState, LOD_LEVEL_COUNT> lodStates{};

        void calculateBoundingSphere()
        {
            glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
            float radius = glm::length(aabbMax - center);
            boundingSphere = glm::vec4(center, radius);
        }

        bool hasRenderableLOD() const
        {
            for (const auto& state : lodStates)
            {
                if (state == LODStreamState::Ready) return true;
            }
            return false;
        }

        uint32_t getAvailableLODMask() const
        {
            uint32_t mask = 0;
            for (uint32_t i = 0; i < LOD_LEVEL_COUNT; ++i)
            {
                if (lodStates[i] == LODStreamState::Ready)
                {
                    mask |= (1u << i);
                }
            }
            return mask;
        }

        bool hasMeshletData() const
        {
            for (const auto& mlod : meshletLods)
            {
                if (mlod.meshletCount > 0) return true;
            }
            return false;
        }

    };

    struct MergedMeshInfo
    {
        std::string meshPath;
        std::vector<SubmeshLocation> submeshes;
        uint32_t firstSubmeshIndex;
        uint32_t submeshCount;
        glm::vec3 aabbMin;
        glm::vec3 aabbMax;
    };

    struct GPUDrivenStats
    {
        uint32_t totalObjects;
        uint32_t visibleObjects;
        uint32_t drawCalls;
        uint32_t objectsLOD0;
        uint32_t objectsLOD1;
        uint32_t objectsLOD2;
        uint32_t objectsLOD3;
        uint32_t culledByFrustum;
        uint32_t culledByOcclusion;
    };

    struct MeshTasksIndirectCommand
    {
        uint32_t groupCountX;
        uint32_t groupCountY;
        uint32_t groupCountZ;
    };
    static_assert(sizeof(MeshTasksIndirectCommand) == 12);

    struct alignas(32) BatchDrawStats
    {
        uint32_t drawCount;
        uint32_t lodCount0;
        uint32_t lodCount1;
        uint32_t lodCount2;
        uint32_t lodCount3;
        uint32_t culledByFrustum;
        uint32_t culledByOcclusion;
        uint32_t padding;
    };
    static_assert(sizeof(BatchDrawStats) == 32);
}
