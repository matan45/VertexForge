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

        inline constexpr uint32_t MAX_POINT_SHADOW_CASTERS = 32;

        inline constexpr uint32_t DEFAULT_CSM_CASCADES = 4;

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
        Cube = 0
    };

    struct ShadowResourceHandle
    {
        ShadowResourceType resourceType = ShadowResourceType::Cube;
        uint32_t resourceIndex = std::numeric_limits<uint32_t>::max();
        uint32_t layerOrFace = 0;

        [[nodiscard]] bool isValid() const
        {
            return resourceIndex != std::numeric_limits<uint32_t>::max();
        }

        void invalidate()
        {
            resourceType = ShadowResourceType::Cube;
            resourceIndex = std::numeric_limits<uint32_t>::max();
            layerOrFace = 0;
        }

        [[nodiscard]] bool isCube() const { return resourceType == ShadowResourceType::Cube; }
    };

    struct ShadowSettings
    {
        uint32_t resolution = 1024;
        ShadowQuality quality = ShadowQuality::High;

        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        uint32_t cascadeCount = ShadowConstants::DEFAULT_CSM_CASCADES;
        float cascadeSplitLambda = 0.75f;

        float lightSize = 1.0f;

        bool enabled = true;
        bool castShadows = true;
    };

    struct ShadowView
    {
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewProjectionMatrix{1.0f};

        glm::vec4 lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
        glm::vec4 lightPosition{0.0f, 0.0f, 0.0f, 1.0f};

        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        float texelSize = 1.0f / 2048.0f;

        float lightSize = 1.0f;
        bool filterEnabled = true;

        uint32_t entityId = 0;
        bool cached = false;

        uint16_t cascadeIndex = 0;
        uint32_t layer = 0;
        ShadowMapType type = ShadowMapType::None;

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
        ShadowResourceHandle resourceHandle; // for point light cubemaps

        uint32_t lightEntityId = 0;

        bool isStatic = false;
        bool shadowCached = false;
        uint32_t lastRenderedFrame = 0;
        uint32_t renderedFrameCount = 0;

        bool matricesDirty = true;
        bool settingsDirty = true;

        // VSM page tracking
        uint32_t vsmLightIndex = 0;
        uint32_t vsmPagesX = 0;
        uint32_t vsmPagesY = 0;
        uint32_t vsmPageTableOffset = 0;
        std::vector<uint32_t> vsmPhysicalTiles;
        std::vector<uint32_t> vsmPageLastUsedFrame; // per-page frame counter for eviction
        std::vector<bool> vsmPageDirty;             // per-page dirty flag for incremental rendering

        void invalidate()
        {
            for (auto& view : views)
            {
                view.type = ShadowMapType::None;
            }
            resourceHandle.invalidate();
            matricesDirty = true;
            shadowCached = false;
        }

        void invalidateCache()
        {
            shadowCached = false;
            renderedFrameCount = 0;
            for (auto& view : views)
            {
                view.cached = false;
            }
            // Mark all VSM pages dirty for re-rendering
            for (size_t i = 0; i < vsmPageDirty.size(); ++i)
            {
                vsmPageDirty[i] = true;
            }
        }

        [[nodiscard]] bool usesVSM() const
        {
            return type == ShadowMapType::Spot2D || type == ShadowMapType::Directional2D || type == ShadowMapType::DirectionalCSM;
        }
    };

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

    struct TerrainShadowPassParams
    {
        vk::DescriptorSet terrainDataDescSet;    // Terrain tile GPU data buffer
        vk::DescriptorSet terrainMeshletDescSet; // Terrain meshlet buffer
        vk::DescriptorSet terrainVertexDescSet;  // Terrain vertex buffer
        uint32_t tileCount = 0;
        uint32_t shadowLOD = 2;  // Default to LOD 2 (coarse) for shadows
    };

}
