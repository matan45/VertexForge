#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>
#include <cmath>
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

            // Use dot product for robust pole detection (not just Y component)
            glm::vec3 worldUp = (std::abs(glm::dot(axes.lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.999f)
                ? glm::vec3(0.0f, 1.0f, 0.0f)
                : glm::vec3(1.0f, 0.0f, 0.0f);

            axes.lightRight = glm::normalize(glm::cross(worldUp, axes.lightDir));
            axes.lightUp = glm::normalize(glm::cross(axes.lightDir, axes.lightRight));
            return axes;
        }
    };

    inline bool matrixChanged(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-6f)
    {
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (std::abs(a[i][j] - b[i][j]) > epsilon)
                    return true;
        return false;
    }

    namespace ShadowConstants
    {
        inline constexpr uint32_t CUBE_FACE_COUNT = 6;

        inline constexpr uint32_t MAX_POINT_SHADOW_CASTERS = 32;

        inline constexpr float DEFAULT_DEPTH_BIAS = 0.005f;
        inline constexpr float DEFAULT_SLOPE_BIAS = 1.5f;
        inline constexpr float DEFAULT_NORMAL_BIAS = 0.02f;

        // 272 for point(32*6=192)+spot, plus headroom for directional clipmap levels
        // (one VSM view per level, see DirectionalShadowCalculator).
        inline constexpr uint32_t MAX_TOTAL_SHADOW_VIEWS = 320;

        // Directional clipmap defaults (see DirectionalShadowCalculator).
        inline constexpr uint32_t DEFAULT_CLIPMAP_LEVELS = 6;
        inline constexpr float DEFAULT_CLIPMAP_BASE_EXTENT = 32.0f; // half-size of level 0 in world units
        inline constexpr float DEFAULT_CLIPMAP_DEPTH_RANGE = 4000.0f;
    }

    enum class ShadowMapType : uint8_t
    {
        None = 0,
        PointCube,
        Spot2D,
        Directional
    };

    enum class ShadowQuality : uint8_t
    {
        Off = 0,
        Low,
        Medium,
        High,
        Ultra
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

        float lightSize = 1.0f;
        float maxShadowDistance = 200.0f;

        // Directional clipmap (ShadowMapType::Directional only)
        uint32_t clipmapLevelCount = ShadowConstants::DEFAULT_CLIPMAP_LEVELS;
        float clipmapBaseExtent = ShadowConstants::DEFAULT_CLIPMAP_BASE_EXTENT;
        float clipmapDepthRange = ShadowConstants::DEFAULT_CLIPMAP_DEPTH_RANGE;

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

        uint32_t lightEntityId = 0;

        bool isStatic = false;
        // Directional clipmaps move with the camera every frame (never "static"), but must
        // allocate physical tiles on demand from screen-space feedback rather than eagerly
        // (a full clipmap is thousands of virtual pages). This flag routes them through the
        // feedback allocation path instead of the eager non-static path.
        bool feedbackDriven = false;
        bool shadowCached = false;
        uint32_t lastRenderedFrame = 0;
        uint32_t renderedFrameCount = 0;

        bool matricesDirty = true;
        bool settingsDirty = true;

        // Shadow streaming priority (computed per frame based on distance/importance)
        float shadowPriority = 1.0f;
        uint32_t maxPagesOverride = 0;

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

        void invalidate()
        {
            for (auto& view : views)
            {
                view.type = ShadowMapType::None;
            }
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
            return type == ShadowMapType::Spot2D ||
                   type == ShadowMapType::PointCube ||
                   type == ShadowMapType::Directional;
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
    };

}
