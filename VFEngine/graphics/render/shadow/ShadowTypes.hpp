#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace render::shadow
{
    namespace ShadowConstants
    {
        inline constexpr uint32_t CUBE_FACE_COUNT = 6;

        inline constexpr uint32_t DEFAULT_ATLAS_SIZE = 4096;

        inline constexpr uint32_t MAX_POINT_SHADOW_CASTERS = 32;

        inline constexpr uint32_t DEFAULT_CSM_CASCADES = 4;

        inline constexpr uint32_t RESOLUTION_LOW = 512;
        inline constexpr uint32_t RESOLUTION_MEDIUM = 1024;
        inline constexpr uint32_t RESOLUTION_HIGH = 2048;
        inline constexpr uint32_t RESOLUTION_ULTRA = 4096;

        inline constexpr uint32_t DEFAULT_SPOT_RESOLUTION = RESOLUTION_MEDIUM;

        inline constexpr float DEFAULT_DEPTH_BIAS = 0.005f;
        inline constexpr float DEFAULT_SLOPE_BIAS = 1.5f;
        inline constexpr float DEFAULT_NORMAL_BIAS = 0.02f;

        inline constexpr uint32_t MAX_TOTAL_SHADOW_VIEWS = 272;
    }

    enum class ShadowMapType : uint8_t
    {
        None = 0,
        Directional2D,
        DirectionalCSM,
        PointCube,
        Spot2D
    };

    enum class ShadowFilterMode : uint8_t
    {
        None = 0,
        PCF,
        PCSS,
        VSM
    };

    enum class ShadowQuality : uint8_t
    {
        Off = 0,
        Low,
        Medium,
        High,
        Ultra
    };

    enum class ShadowResourceType : uint8_t
    {
        Atlas = 0,
        Array,
        Cube
    };

    struct ShadowResourceHandle
    {
        ShadowResourceType resourceType = ShadowResourceType::Atlas;
        uint32_t resourceIndex = std::numeric_limits<uint32_t>::max();
        uint32_t layerOrFace = 0;

        [[nodiscard]] bool isValid() const
        {
            return resourceIndex != std::numeric_limits<uint32_t>::max();
        }

        void invalidate()
        {
            resourceType = ShadowResourceType::Atlas;
            resourceIndex = std::numeric_limits<uint32_t>::max();
            layerOrFace = 0;
        }

        [[nodiscard]] bool isArray() const { return resourceType == ShadowResourceType::Array; }
        [[nodiscard]] bool isCube() const { return resourceType == ShadowResourceType::Cube; }
    };

    struct ShadowMapHandle
    {
        uint32_t atlasIndex = std::numeric_limits<uint32_t>::max();
        uint32_t layer = 0;
        uint16_t cascadeIndex = 0;
        ShadowMapType type = ShadowMapType::None;

        [[nodiscard]] bool isValid() const
        {
            return atlasIndex != std::numeric_limits<uint32_t>::max() &&
                   type != ShadowMapType::None;
        }

        void invalidate()
        {
            atlasIndex = std::numeric_limits<uint32_t>::max();
            layer = 0;
            cascadeIndex = 0;
            type = ShadowMapType::None;
        }
    };

    struct ShadowSettings
    {
        uint32_t resolution = ShadowConstants::DEFAULT_SPOT_RESOLUTION;
        ShadowFilterMode filterMode = ShadowFilterMode::PCF;
        ShadowQuality quality = ShadowQuality::High;

        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        uint32_t cascadeCount = ShadowConstants::DEFAULT_CSM_CASCADES;
        float cascadeSplitLambda = 0.75f;

        float softness = 1.0f;

        bool enabled = true;
        bool castShadows = true;
    };

    struct ShadowView
    {
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewProjectionMatrix{1.0f};

        glm::vec4 atlasViewport{0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec4 lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
        glm::vec4 lightPosition{0.0f, 0.0f, 0.0f, 1.0f};

        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        float texelSize = 1.0f / static_cast<float>(ShadowConstants::RESOLUTION_HIGH);

        uint8_t pcfKernelRadius = 2;
        float pcfSoftness = 1.0f;
        bool filterEnabled = true;

        uint32_t entityId = 0;

        ShadowMapHandle handle;

        void updateViewProjection()
        {
            viewProjectionMatrix = projectionMatrix * viewMatrix;
        }
    };

    struct LightShadowData
    {
        ShadowSettings settings;
        ShadowMapType type = ShadowMapType::None;

        std::vector<ShadowView> views;
        ShadowResourceHandle resourceHandle;

        uint32_t lightEntityId = 0;

        bool matricesDirty = true;
        bool settingsDirty = true;

        void invalidate()
        {
            for (auto& view : views)
            {
                view.handle.invalidate();
            }
            resourceHandle.invalidate();
            matricesDirty = true;
        }

        [[nodiscard]] bool usesAtlas() const
        {
            return type == ShadowMapType::Spot2D || type == ShadowMapType::Directional2D || type == ShadowMapType::DirectionalCSM;
        }
    };

    struct alignas(16) GPUShadowData
    {
        glm::mat4 viewProjection;
        glm::vec4 atlasViewport;
        glm::vec4 biasParams;    // x=depthBias, y=slopeBias, z=normalBias, w=texelSize
        glm::vec4 rangeParams;   // x=near, y=far, z=cascadeCount, w=cascadeIndex
        glm::vec4 pcfParams;     // x=kernelRadius, y=softness, z=filterEnabled, w=cubeMapIndex
    };
    static_assert(sizeof(GPUShadowData) == 128, "GPUShadowData must be 128 bytes");

    struct ShadowDebugInfo
    {
        ShadowMapType type = ShadowMapType::None;
        uint32_t cascadeIndex = 0;
        uint32_t entityId = 0;
        glm::mat4 viewProjectionMatrix{1.0f};
        glm::vec3 lightPosition{0.0f};
        glm::vec3 lightDirection{0.0f, -1.0f, 0.0f};
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct ShadowAtlasTile
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layer = 0;
        bool allocated = false;
        ShadowMapHandle owner;

        [[nodiscard]] glm::vec4 getNormalizedViewport(uint32_t atlasWidth, uint32_t atlasHeight) const
        {
            return glm::vec4(
                static_cast<float>(x) / static_cast<float>(atlasWidth),
                static_cast<float>(y) / static_cast<float>(atlasHeight),
                static_cast<float>(width) / static_cast<float>(atlasWidth),
                static_cast<float>(height) / static_cast<float>(atlasHeight)
            );
        }
    };

    struct ShadowLODConfig
    {
        bool enabled = true;
        float tier0Distance = 30.0f;   // Resolution tier 0 max distance
        float tier1Distance = 80.0f;   // Resolution tier 1 max distance
        float tier2Distance = 150.0f;  // Resolution tier 2 max distance

        uint32_t tier0Resolution = 2048;
        uint32_t tier1Resolution = 1024;
        uint32_t tier2Resolution = 512;

        uint32_t getResolutionForDistance(float distance) const
        {
            if (!enabled) return tier0Resolution;
            if (distance < tier0Distance) return tier0Resolution;
            if (distance < tier1Distance) return tier1Resolution;
            if (distance < tier2Distance) return tier2Resolution;
            return 0; // No shadow beyond tier2
        }

        bool shouldHaveShadow(float distance) const
        {
            if (!enabled) return true;
            return distance < tier2Distance;
        }
    };

    struct TerrainShadowPassParams
    {
        vk::DescriptorSet terrainDataDescSet;    // Terrain tile GPU data buffer
        vk::DescriptorSet terrainMeshletDescSet; // Terrain meshlet buffer
        vk::DescriptorSet terrainVertexDescSet;  // Terrain vertex buffer
        uint32_t tileCount = 0;
        uint32_t shadowLOD = 2;  // Default to LOD 2 (coarse) for shadows
    };

    struct VegetationShadowPassParams
    {
        vk::DescriptorSet meshletDescSet;   // Shared meshlet buffer
        vk::DescriptorSet vertexDescSet;    // Shared vertex buffer
        uint32_t instanceCount = 0;
        uint32_t shadowLOD = 1;  // Default to LOD 1 for vegetation shadows
    };
}
