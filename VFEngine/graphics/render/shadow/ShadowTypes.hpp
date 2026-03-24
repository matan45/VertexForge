#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <limits>
#include <vector>

namespace render::shadow
{
    struct CameraContext
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct LightSpaceAxes
    {
        glm::vec3 lightDir{0.0f, -1.0f, 0.0f};
        glm::vec3 lightRight{1.0f, 0.0f, 0.0f};
        glm::vec3 lightUp{0.0f, 0.0f, 1.0f};

        static LightSpaceAxes fromDirection(const glm::vec3& direction)
        {
            LightSpaceAxes axes;
            axes.lightDir = glm::normalize(direction);
            glm::vec3 worldUp = (std::abs(axes.lightDir.y) < 0.99f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);
            axes.lightRight = glm::normalize(glm::cross(worldUp, axes.lightDir));
            axes.lightUp = glm::cross(axes.lightDir, axes.lightRight);
            return axes;
        }
    };

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
        Spot2D,
        DirectionalClipmap
    };

    enum class ShadowQuality : uint8_t
    {
        Off = 0,
        Low,
        Medium,
        High,
        Ultra
    };

    struct ShadowResourceHandle
    {
        uint32_t resourceIndex = std::numeric_limits<uint32_t>::max();
        uint32_t layerOrFace = 0;

        [[nodiscard]] bool isValid() const
        {
            return resourceIndex != std::numeric_limits<uint32_t>::max();
        }

        void invalidate()
        {
            resourceIndex = std::numeric_limits<uint32_t>::max();
            layerOrFace = 0;
        }
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

        // Clipmap settings
        uint32_t clipmapLevelCount = 16;
        float clipmapBaseExtent = 5.0f;
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
        std::vector<uint32_t> vsmPhysicalTiles;     // static layer tiles
        std::vector<uint32_t> vsmPageLastUsedFrame;  // per-page frame counter for eviction
        std::vector<bool> vsmPageDirty;              // per-page dirty flag for static layer

        // Dual-layer: dynamic tile overlay (only for non-static lights)
        std::vector<uint32_t> vsmDynamicTiles;       // dynamic tile per page (INVALID_TILE = none allocated)
        std::vector<bool> vsmPageHasDynamic;          // true if page has dynamic geometry this frame
        std::vector<uint32_t> vsmDynamicTileLastUsedFrame; // for cooldown before freeing dynamic tiles

        // Light movement tracking: detect when VP matrix changes to invalidate cached pages
        glm::mat4 lastViewProjection{0.0f}; // initialized to zero so first frame always dirty

        // Clipmap tracking (only used when type == DirectionalClipmap)
        std::vector<glm::vec2> clipmapLastSnapPositions;  // per-level snap position for dirty detection
        std::vector<uint32_t> clipmapLevelPageOffsets;     // per-level offset within page table block
        std::vector<uint32_t> clipmapLevelPagesPerSide;    // per-level page grid dimension (variable density)

        // Toroidal scrolling state (per-level, only for DirectionalClipmap)
        std::vector<glm::ivec2> clipmapScrollOffset;     // scroll offset in page units, [0, pps)
        std::vector<glm::vec2>  clipmapPageGridOrigin;   // light-space XY origin snapped to page boundaries
        std::vector<glm::mat4>  clipmapRenderVP;         // page-grid-snapped VP for rendering (stable)
        std::vector<glm::vec2>  clipmapUVOffset;         // per-level UV offset: (texelSnapped - pageGrid) / (2*worldExtent)
        std::vector<bool>       clipmapLevelInitialized; // per-level: true after first dirty-flag pass

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
            // Reset toroidal scroll state
            for (auto& s : clipmapScrollOffset)
                s = glm::ivec2(0);
            for (auto& o : clipmapPageGridOrigin)
                o = glm::vec2(0.0f);
            for (auto& v : clipmapRenderVP)
                v = glm::mat4(1.0f);
            for (auto& u : clipmapUVOffset)
                u = glm::vec2(0.0f);
        }

        [[nodiscard]] bool usesVSM() const
        {
            return type == ShadowMapType::Spot2D || type == ShadowMapType::Directional2D
                || type == ShadowMapType::DirectionalCSM || type == ShadowMapType::DirectionalClipmap;
        }

        [[nodiscard]] bool isDirectionalType() const
        {
            return type == ShadowMapType::DirectionalCSM
                || type == ShadowMapType::Directional2D
                || type == ShadowMapType::DirectionalClipmap;
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
        uint32_t shadowLOD = 0;  // LOD 0 for accurate terrain self-shadows
    };

}
