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
        Ready,
        Evicted
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
        glm::vec4 boundingSphere;
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
    static_assert(sizeof(GPUObjectData) == 320);


    namespace ObjectFlags
    {
        constexpr uint32_t AlphaMask = 1 << 4;
        constexpr uint32_t UniformScale = 1 << 9;
    }


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
        uint32_t padding3;
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

        uint32_t getBestAvailableLOD(uint32_t requestedLOD) const
        {
            for (uint32_t lod = requestedLOD; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (lodStates[lod] == LODStreamState::Ready) return lod;
            }
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (lodStates[lod] == LODStreamState::Ready) return lod;
            }
            return LOD_LEVEL_COUNT;
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

        uint32_t getBestAvailableMeshletLOD(uint32_t requestedLOD) const
        {
            for (uint32_t lod = requestedLOD; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (meshletLods[lod].meshletCount > 0 &&
                    lodStates[lod] == LODStreamState::Ready)
                {
                    return lod;
                }
            }
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                if (meshletLods[lod].meshletCount > 0 &&
                    lodStates[lod] == LODStreamState::Ready)
                {
                    return lod;
                }
            }
            return LOD_LEVEL_COUNT;
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
