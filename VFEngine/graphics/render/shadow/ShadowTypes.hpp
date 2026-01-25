#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace render::shadow
{
    // ============================================
    // Shadow System Constants
    // ============================================
    namespace ShadowConstants
    {
        // Default shadow atlas dimensions (4K x 4K)
        inline constexpr uint32_t DEFAULT_ATLAS_SIZE = 4096;

        // Maximum supported shadow maps per type
        inline constexpr uint32_t MAX_DIRECTIONAL_SHADOW_CASTERS = 4;   // Usually 1-2, CSM uses single light
        inline constexpr uint32_t MAX_POINT_SHADOW_CASTERS = 32;        // Cube maps (6 faces each)
        inline constexpr uint32_t MAX_SPOT_SHADOW_CASTERS = 64;         // 2D shadow maps

        // CSM (Cascaded Shadow Map) settings
        inline constexpr uint32_t MAX_CSM_CASCADES = 4;
        inline constexpr uint32_t DEFAULT_CSM_CASCADES = 4;

        // Default shadow map resolutions
        inline constexpr uint32_t RESOLUTION_LOW = 512;
        inline constexpr uint32_t RESOLUTION_MEDIUM = 1024;
        inline constexpr uint32_t RESOLUTION_HIGH = 2048;
        inline constexpr uint32_t RESOLUTION_ULTRA = 4096;

        inline constexpr uint32_t DEFAULT_DIRECTIONAL_RESOLUTION = RESOLUTION_HIGH; // Per cascade
        inline constexpr uint32_t DEFAULT_POINT_RESOLUTION = RESOLUTION_LOW;         // Per cube face
        inline constexpr uint32_t DEFAULT_SPOT_RESOLUTION = RESOLUTION_MEDIUM;

        // Shadow bias defaults
        inline constexpr float DEFAULT_DEPTH_BIAS = 0.005f;
        inline constexpr float DEFAULT_SLOPE_BIAS = 1.5f;
        inline constexpr float DEFAULT_NORMAL_BIAS = 0.02f;

        // Maximum total shadow views (for buffer allocation)
        // Directional: 4 casters * 4 cascades = 16
        // Point: 32 casters * 6 faces = 192
        // Spot: 64 casters * 1 = 64
        // Total: 272
        inline constexpr uint32_t MAX_TOTAL_SHADOW_VIEWS = 272;
    }

    // ============================================
    // Enums
    // ============================================
    enum class ShadowMapType : uint8_t
    {
        None = 0,
        Directional2D,      // Single 2D shadow map (simple directional)
        DirectionalCSM,     // Cascaded Shadow Maps
        PointCube,          // Omnidirectional cube map (6 faces)
        Spot2D              // Perspective 2D shadow map
    };

    enum class ShadowFilterMode : uint8_t
    {
        None = 0,           // Hard shadows (no filtering)
        PCF,                // Percentage Closer Filtering
        PCSS,               // Percentage Closer Soft Shadows (future)
        VSM                 // Variance Shadow Maps (future)
    };

    enum class ShadowQuality : uint8_t
    {
        Low = 0,            // 512px
        Medium,             // 1024px
        High,               // 2048px
        Ultra               // 4096px
    };

    /**
     * Resource type for shadow textures.
     * Determines which resource pool manages the shadow map.
     */
    enum class ShadowResourceType : uint8_t
    {
        Atlas = 0,          // Tile in shared 2D atlas (spot lights)
        Array,              // Layer in dedicated texture array (CSM)
        Cube                // Face in dedicated cube map (point lights)
    };

    // ============================================
    // Handle for dedicated shadow resources (Array/Cube)
    // ============================================
    struct ShadowResourceHandle
    {
        ShadowResourceType resourceType = ShadowResourceType::Atlas;
        uint32_t resourceIndex = std::numeric_limits<uint32_t>::max();  // Index in resource pool
        uint32_t layerOrFace = 0;             // Array layer (0-3 for CSM) or cube face (0-5)

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

        [[nodiscard]] bool isAtlas() const { return resourceType == ShadowResourceType::Atlas; }
        [[nodiscard]] bool isArray() const { return resourceType == ShadowResourceType::Array; }
        [[nodiscard]] bool isCube() const { return resourceType == ShadowResourceType::Cube; }
    };

    // ============================================
    // Handle for referencing shadow maps in atlas
    // ============================================
    struct ShadowMapHandle
    {
        uint32_t atlasIndex = std::numeric_limits<uint32_t>::max();  // Tile index in atlas
        uint32_t layer = 0;              // For cube maps: face index (0-5)
        uint16_t cascadeIndex = 0;       // For CSM: cascade level (0-3)
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

    // ============================================
    // Per-light shadow configuration
    // ============================================
    struct ShadowSettings
    {
        // Quality settings
        uint32_t resolution = ShadowConstants::DEFAULT_SPOT_RESOLUTION;
        ShadowFilterMode filterMode = ShadowFilterMode::PCF;
        ShadowQuality quality = ShadowQuality::High;

        // Bias settings to prevent shadow acne
        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        // Range settings
        float nearPlane = 0.1f;
        float farPlane = 100.0f;          // For point/spot lights

        // CSM-specific (for directional lights)
        uint32_t cascadeCount = ShadowConstants::DEFAULT_CSM_CASCADES;
        float cascadeSplitLambda = 0.75f;  // Logarithmic vs linear split blend

        // Soft shadow settings
        float softness = 1.0f;             // For PCF/PCSS kernel size
        uint32_t sampleCount = 16;         // PCF tap count

        bool enabled = true;
        bool castShadows = true;

        // Get resolution from quality level
        [[nodiscard]] static uint32_t getResolutionForQuality(ShadowQuality q)
        {
            switch (q)
            {
                case ShadowQuality::Low:    return ShadowConstants::RESOLUTION_LOW;
                case ShadowQuality::Medium: return ShadowConstants::RESOLUTION_MEDIUM;
                case ShadowQuality::High:   return ShadowConstants::RESOLUTION_HIGH;
                case ShadowQuality::Ultra:  return ShadowConstants::RESOLUTION_ULTRA;
                default: return ShadowConstants::RESOLUTION_HIGH;
            }
        }
    };

    // ============================================
    // View matrix and projection for shadow rendering
    // ============================================
    struct ShadowView
    {
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 viewProjectionMatrix{1.0f};

        // Viewport in atlas (normalized 0-1)
        glm::vec4 atlasViewport{0.0f, 0.0f, 1.0f, 1.0f};  // x, y, width, height

        // For shader use
        glm::vec4 lightDirection{0.0f, -1.0f, 0.0f, 0.0f};  // Directional/spot
        glm::vec4 lightPosition{0.0f, 0.0f, 0.0f, 1.0f};    // Point/spot

        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        // Per-light bias settings (copied from LightShadowData::settings)
        float depthBias = ShadowConstants::DEFAULT_DEPTH_BIAS;
        float slopeBias = ShadowConstants::DEFAULT_SLOPE_BIAS;
        float normalBias = ShadowConstants::DEFAULT_NORMAL_BIAS;

        // Texel size for PCF filtering (1.0 / resolution)
        float texelSize = 1.0f / static_cast<float>(ShadowConstants::RESOLUTION_HIGH);

        // PCF filtering params (set during beginFrame from global/per-light settings)
        uint8_t pcfKernelRadius = 1;  // 0=none (hard), 1=3x3, 2=5x5, 3=7x7
        float pcfSoftness = 1.0f;     // Kernel spread multiplier
        bool filterEnabled = true;    // Soft shadows toggle

        ShadowMapHandle handle;

        void updateViewProjection()
        {
            viewProjectionMatrix = projectionMatrix * viewMatrix;
        }
    };

    // ============================================
    // Per-light shadow metadata (stored per entity)
    // ============================================
    struct LightShadowData
    {
        ShadowSettings settings;
        ShadowMapType type = ShadowMapType::None;

        // Shadow views (1 for spot, 6 for point, 1-4 for CSM)
        // For atlas-based shadows (Spot2D, Directional2D): views[i].handle contains atlas tile info
        // For dedicated resources (DirectionalCSM, PointCube): views store matrices, resourceHandle stores texture
        std::vector<ShadowView> views;

        // Dedicated resource handle for CSM arrays and point cube maps
        // Only valid when type is DirectionalCSM or PointCube
        // For Spot2D/Directional2D, this is unused (atlas tiles are in views[i].handle)
        ShadowResourceHandle resourceHandle;

        // Entity ID of the light this shadow data belongs to
        uint32_t lightEntityId = 0;

        // Dirty flags for optimization
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

        [[nodiscard]] uint32_t getViewCount() const
        {
            return static_cast<uint32_t>(views.size());
        }

        [[nodiscard]] bool usesAtlas() const
        {
            return type == ShadowMapType::Spot2D || type == ShadowMapType::Directional2D;
        }

        [[nodiscard]] bool usesDedicatedResource() const
        {
            return type == ShadowMapType::DirectionalCSM || type == ShadowMapType::PointCube;
        }
    };

    // ============================================
    // GPU-aligned shadow data for shader consumption
    // ============================================
    struct alignas(16) GPUShadowData
    {
        glm::mat4 viewProjection;         // 64 bytes - light space transform
        glm::vec4 atlasViewport;          // 16 bytes - xy=offset, zw=size (normalized 0-1)
        glm::vec4 biasParams;             // 16 bytes - x=depthBias, y=slopeBias, z=normalBias, w=texelSize
        glm::vec4 rangeParams;            // 16 bytes - x=near, y=far, z=1/(far-near), w=cascadeIndex
        glm::vec4 pcfParams;              // 16 bytes - x=kernelRadius (0-3), y=softness, z=filterEnabled, w=reserved
    };
    static_assert(sizeof(GPUShadowData) == 128, "GPUShadowData must be 128 bytes");
    static_assert(offsetof(GPUShadowData, viewProjection) == 0, "GPUShadowData::viewProjection offset mismatch");
    static_assert(offsetof(GPUShadowData, atlasViewport) == 64, "GPUShadowData::atlasViewport offset mismatch");
    static_assert(offsetof(GPUShadowData, biasParams) == 80, "GPUShadowData::biasParams offset mismatch");
    static_assert(offsetof(GPUShadowData, rangeParams) == 96, "GPUShadowData::rangeParams offset mismatch");
    static_assert(offsetof(GPUShadowData, pcfParams) == 112, "GPUShadowData::pcfParams offset mismatch");

    // ============================================
    // Shadow counts for shader
    // ============================================
    struct alignas(16) GPUShadowCounts
    {
        uint32_t directionalCount;        // Number of directional shadow views
        uint32_t pointCount;              // Number of point shadow views (x6 for cube faces)
        uint32_t spotCount;               // Number of spot shadow views
        uint32_t totalCount;              // Total shadow views
    };
    static_assert(sizeof(GPUShadowCounts) == 16, "GPUShadowCounts must be 16 bytes");

    // ============================================
    // Debug visualization info
    // ============================================
    struct ShadowDebugInfo
    {
        ShadowMapType type = ShadowMapType::None;
        uint32_t cascadeIndex = 0;           // For CSM: cascade level (0-3)
        uint32_t entityId = 0;               // Light entity ID
        glm::mat4 viewProjectionMatrix{1.0f};// For frustum reconstruction (inverse for rendering)
        glm::vec3 lightPosition{0.0f};       // Light position in world space
        glm::vec3 lightDirection{0.0f, -1.0f, 0.0f}; // Light direction (for spot/directional)
        float nearPlane = 0.1f;
        float farPlane = 100.0f;             // For point lights, this is the radius
    };

    // ============================================
    // Atlas tile allocation info
    // ============================================
    struct ShadowAtlasTile
    {
        uint32_t x = 0;                   // Pixel offset in atlas
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layer = 0;               // Array layer (for cube map atlas)
        bool allocated = false;
        ShadowMapHandle owner;            // Which shadow map owns this tile

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
}
