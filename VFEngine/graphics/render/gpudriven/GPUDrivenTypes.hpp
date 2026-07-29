#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <string>
#include "MeshletBufferTypes.hpp"
#include "../../../services/data/CullingCategories.hpp"
#include "../common/CameraTypes.hpp"
#include "../../core/BindlessConstants.hpp"

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

    constexpr uint32_t MAX_GPU_OBJECTS = 262144;
    constexpr uint32_t MAX_DRAW_COMMANDS = 700000;
    constexpr uint32_t DEFAULT_BATCH_COUNT = 4;
    constexpr uint32_t MAX_BATCH_COUNT = 8;
    using core::MAX_BINDLESS_TEXTURES;
    constexpr uint32_t MAX_SHADER_GROUPS = 16;
    constexpr uint32_t LOD_LEVEL_COUNT = 4;
    constexpr uint32_t TERRAIN_LOD_LEVEL_COUNT = 6;
    constexpr uint32_t CULL_WORKGROUP_SIZE = 64;
    using core::INVALID_TEXTURE_INDEX;

    // enableLODSelection values — must match camera_types.glsl constants
    constexpr uint32_t LOD_SELECTION_DISABLED       = 0;
    constexpr uint32_t LOD_SELECTION_ENABLED        = 1;
    constexpr uint32_t LOD_SELECTION_WITH_CROSSFADE = 2;

    constexpr uint32_t MAX_BONES_PER_OBJECT = 128;
    constexpr uint32_t MAX_ANIMATED_OBJECTS = 4096;
    constexpr uint32_t INVALID_BONE_OFFSET = 0xFFFFFFFF;

    constexpr uint32_t MAX_GPU_INSTANCES = 262144;  // Max instance transforms in SSBO (VK-1573: 131072->262144 for foliage)

    // Per-instance data for instanced draw calls. Includes PBR override fields so that
    // entities with different .vfMatInstance scalar overrides (but same parent material)
    // can be batched into a single draw call.
    //
    // Trade-off: 112 bytes vs 64 bytes (mat4 only). The extra 48 bytes per instance
    // increase GPU memory and bandwidth (~12 MB of that at MAX_GPU_INSTANCES = 262144;
    // total device SSBO ~28 MB, plus one host-visible staging copy per frame-in-flight).
    // iblOverride.w acts as a hasOverride flag: 0.0 = use PerDrawData PBR (no overhead
    // in the shader's common path), 1.0 = use per-instance PBR values.
    struct alignas(16) GPUInstanceTransform
    {
        glm::mat4 modelMatrix;
        glm::vec4 albedoOverride{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 pbrOverride{0.0f, 0.5f, 1.0f, 0.0f};   // metallic, roughness, ao, emission
        glm::vec4 iblOverride{1.0f, 0.5f, 0.0f, 0.0f};   // iblDiffuse, iblSpecular, alphaCutoff, hasOverride
    };
    static_assert(sizeof(GPUInstanceTransform) == 112);

    constexpr uint32_t SHADER_GROUP_TRANSPARENT = 3;  // Translucent objects (alpha blend / WBOIT)
    constexpr uint32_t SHADER_GROUP_BLEND = 4;        // Additive / Multiply objects

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
        glm::vec4 aabbMin;  // .w = maxDrawDistanceSquared (0 = use category default)
        // IMPORTANT: aabbMax.w is overloaded to store instanceCount as a uint32_t via memcpy
        // (not float cast). GPU reads it with floatBitsToUint(). Do NOT use aabbMax.w as a float.
        glm::vec4 aabbMax;  // .w = instanceCount (uint via memcpy/floatBitsToUint, 0 or 1 = non-instanced)
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
        // .w=instanceOffset; .y=startFadeDistanceSquared bits (VK-1582, packed via memcpy of a float,
        // read on GPU with uintBitsToFloat; 0 = no near fade). .x/.z spare.
        glm::uvec4 instanceData{INVALID_TEXTURE_INDEX, 0, 0, 0};
    };
    static_assert(sizeof(GPUObjectData) == 352);

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
        constexpr uint32_t Selected = 1 << 13;
        constexpr uint32_t Billboard = 1 << 14;
        constexpr uint32_t Instanced = 1 << 15;
        constexpr uint32_t ShadowStatic = 1 << 17;  // Object's shadow geometry is cacheable (from TransformComponent::isStatic)

        // VK-1415: per-object render-layer index (0-31) packed in flags bits 18-22 (bits 23-31 stay
        // free). Expanded in the cull shader to a bit (1u << index) and AND-ed with the camera's
        // cullingMask. Must match LAYER_SHIFT/LAYER_MASK in resources/shaders/common/gpu_draw_functions.glsl.
        constexpr uint32_t LayerShift = 18;
        constexpr uint32_t LayerMask  = 0x1Fu;

        // VK-1493: toon shading. Bits 23-24 = shading model (0 = DefaultLit, 1 = Unlit,
        // 2 = Toon), bits 25-31 = toon profile index (0-127, index 0 = built-in default).
        // These are the last free flag bits; do not collide with Layer (18-22),
        // Category (13-16), ShadowStatic (17) or Instanced (15). Must match
        // SHADING_MODEL_SHIFT/PROFILE_INDEX_SHIFT in gpu_draw_functions.glsl and the
        // unpack in toon_lighting.glsl. `makePerDrawData` copies flags verbatim, so no
        // PerDrawData struct change is needed.
        constexpr uint32_t ShadingModelShift = 23;
        constexpr uint32_t ShadingModelMask  = 0x3u;   // bits 23-24
        constexpr uint32_t ProfileIndexShift = 25;
        constexpr uint32_t ProfileIndexMask  = 0x7Fu;  // bits 25-31 (128 profiles)

        // ShadingModel enum ids as they appear in the packed flags (mirror of
        // material::ShadingModel ordering — kept as raw ints so this header stays
        // free of a Utilities dependency).
        constexpr uint32_t ShadingModelDefaultLit = 0;
        constexpr uint32_t ShadingModelUnlit      = 1;
        constexpr uint32_t ShadingModelToon       = 2;

        // OR the shading-model id + profile index into an object's flags. Call AFTER
        // flags is zero-initialized and the other bits (blend/layer/etc.) are set.
        inline void packShadingFlags(uint32_t& flags, uint8_t shadingModel, uint8_t profileIndex)
        {
            flags |= (static_cast<uint32_t>(shadingModel) & ShadingModelMask) << ShadingModelShift;
            flags |= (static_cast<uint32_t>(profileIndex) & ProfileIndexMask) << ProfileIndexShift;
        }

        // VK-1580: foliage wind. A single gate bit (bit 8, free) marking a mesh as
        // wind-receiving foliage. Sway amplitude/direction/time are GLOBAL — read from the
        // shared grass WindSystem UBO — so no per-object wind parameters are stored. Set
        // from a per-material "Foliage Wind" flag. Does NOT collide with any packed field
        // (blend 4-12, Instanced 15, Category 13-16, ShadowStatic 17, Layer 18-22,
        // ShadingModel 23-24, ProfileIndex 25-31). `makePerDrawData` copies flags verbatim,
        // so no PerDrawData change is needed. Must match FLAG_FOLIAGE_WIND in
        // resources/shaders/gpudriven/mesh_shader_gpudriven.glsl.
        constexpr uint32_t FoliageWind = 1 << 8;
    }

    namespace ObjectCategory
    {
        using namespace services::CullingCategory;
        constexpr uint32_t CategoryShift = 13;
        constexpr uint32_t CategoryMask = 0xFu << CategoryShift; // bits 13-16
    }

    struct alignas(16) TerrainTileGPUData
    {
        glm::mat4 modelMatrix;
        glm::vec4 boundingSphere;       // xyz = world center, w = radius
        glm::vec4 aabbMin;              // xyz = world AABB min, w = weightMapResolution (33/65/129)
        glm::vec4 aabbMax;              // xyz = world AABB max, w = packed layerIndices[0-3] (uintBitsToFloat)
        glm::uvec4 lod0MeshletData;     // x = meshletOffset, y = meshletCount (total), z = baseVertexOffset, w = mainMeshletCount (surface only, no skirts)
        glm::uvec4 lod1MeshletData;
        glm::uvec4 lod2MeshletData;
        glm::uvec4 lod3MeshletData;
        glm::uvec4 lod4MeshletData;
        glm::uvec4 lod5MeshletData;
        glm::vec4 lodGeometricErrors;   // Per-LOD geometric error thresholds LOD 0-3 (world units)
        glm::vec4 lodGeometricErrors2;  // x=LOD4 error, y=LOD5 error, z=packed layerIndices[4-7], w=unused
        int32_t coordX;
        int32_t coordZ;
        uint32_t flags;
        uint32_t weightMapOffset;       // Byte offset into weight map SSBO
        glm::uvec4 caveMeshletData;     // x = meshletOffset, y = meshletCount, z = baseVertexOffset, w = reserved
    };
    static_assert(sizeof(TerrainTileGPUData) == 272);

    // Every member is a 4-byte scalar on purpose: std430 then gives base alignment 4 and an array
    // stride equal to sizeof, so C++ and gpu_types.glsl agree with no 16-byte rounding. Adding a
    // vec2/vec3/vec4 member would raise the base alignment and silently desync the two layouts.
    struct TerrainLayerGPUData
    {
        uint32_t albedoTextureIndex;   // Bindless index (0 = default white)
        uint32_t normalTextureIndex;   // Bindless index (0 = default)
        float tilingScale;
        uint32_t ormTextureIndex;      // Bindless index (0 = no ORM texture)
        float roughness;               // Scalar fallback when no ORM
        float metallic;                // Scalar fallback when no ORM
        float ao;                      // Scalar fallback when no ORM
        float emissionStrength;        // Emission intensity
        uint32_t emissionTextureIndex; // Bindless index (0 = no emission texture)
        // VK-1609 height-blended compositing. Per-texel height rides the ORM texture's alpha
        // channel, so it costs no extra fetch and no extra bindless slot.
        // 0.0 == linear: the generated composite's mix() then returns EXACTLY 1.0 and the layer's
        // contribution is bit-identical to the pre-VK-1609 weighted average. resolveTerrainLayerPBR
        // clamps to [0, terrain::MAX_HEIGHT_BLEND_CONTRAST]; that bound keeps exp2's argument in
        // [-8, 8] and is REQUIRED for the mix() exactness argument to hold (an infinity here would
        // make 0.0 * y == NaN and poison the linear case).
        float heightBlendContrast;     // 0 = this layer blends linearly
        // VK-1614 (local wetness / snow) reservation — uploaded 0.0 until that story lands, so the
        // epic pays exactly ONE ABI bump. 48 is also a 16-byte multiple, which keeps a later vec4
        // member from silently changing the std430 array stride.
        float reservedPorosity;
        float reservedSnowRetention;
    };
    static_assert(sizeof(TerrainLayerGPUData) == 48);
    static_assert(alignof(TerrainLayerGPUData) == 4);
    static_assert(sizeof(TerrainLayerGPUData) % 16 == 0,
                  "keep a 16-byte multiple so a later vec4 member cannot desync the std430 stride");
    static_assert(offsetof(TerrainLayerGPUData, albedoTextureIndex) == 0);
    static_assert(offsetof(TerrainLayerGPUData, normalTextureIndex) == 4);
    static_assert(offsetof(TerrainLayerGPUData, ormTextureIndex) == 12);
    static_assert(offsetof(TerrainLayerGPUData, emissionTextureIndex) == 32);
    static_assert(offsetof(TerrainLayerGPUData, heightBlendContrast) == 36);
    static_assert(offsetof(TerrainLayerGPUData, reservedPorosity) == 40);
    static_assert(offsetof(TerrainLayerGPUData, reservedSnowRetention) == 44);

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
        uint32_t lodCount4;
        uint32_t lodCount5;
        uint32_t culledByOcclusion;
        uint32_t padding[3];
    };
    static_assert(sizeof(TerrainCullingStats) == 64);

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
        uint32_t instanceCount;     // Number of instances (1 = non-instanced)
        uint32_t blendModeAndOpacity; // low 8 bits: BlendMode enum, bits 16-31: half-float opacity
        glm::uvec4 instanceData{INVALID_TEXTURE_INDEX, 0, 0, 0}; // .w=instanceOffset
    };
    static_assert(sizeof(PerDrawData) == 256);

    using GPUCameraData = ::render::common::GPUCameraData;

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

    struct RTTCameraParams
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPosition;
        float nearPlane;
        float farPlane;
        uint32_t screenWidth = 0;
        uint32_t screenHeight = 0;
        uint32_t cullingMask = 0xFFFFFFFFu; // VK-1415: per-camera render-layer mask for this RTT view
    };

    struct GPUDrivenStats
    {
        uint32_t totalObjects;
        uint32_t totalInstances; // VK-1579: instance-transform total (foliage/vegetation/mesh), bounded by MAX_GPU_INSTANCES
        uint32_t visibleObjects;
        uint32_t drawCalls;
        uint32_t objectsLOD0;
        uint32_t objectsLOD1;
        uint32_t objectsLOD2;
        uint32_t objectsLOD3;
        uint32_t culledByFrustum;
        uint32_t culledByOcclusion;
        uint32_t culledByDistance;
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
        uint32_t culledByDistance;
    };
    static_assert(sizeof(BatchDrawStats) == 32);
}
