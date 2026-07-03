#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <glm/glm.hpp>
#include "VFXCurveTypes.hpp"
#include "VFXScalability.hpp"

namespace vfx
{
    // VK-1453 (Phase 4) — explicit local-space bounds for an effect.
    //   Auto  - derived analytically from the emitter graph when needed.
    //   Fixed - authored min/max box (center +/- extents), captured and stored.
    // Default (Auto, zero extents) reproduces pre-Phase-4 behavior. See
    // VFXBoundsUtil.hpp for computeAutoBounds()/resolveBounds().
    enum class VFXBoundsMode : uint8_t
    {
        Auto = 0,
        Fixed = 1
    };

    struct VFXBounds
    {
        VFXBoundsMode mode = VFXBoundsMode::Auto;
        glm::vec3 center{0.0f};
        glm::vec3 extents{0.0f};
    };
    enum class VFXPropertyType : uint8_t
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Color,
        Int,
        Bool,
        String,
        Curve,
        Gradient
    };

    using VFXPropertyValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4, int32_t, bool, std::string,
                                          VFXCurve, VFXGradient>;

    enum class VFXNodeType : uint8_t
    {
        Emitter,
        OutSystem,
        ColorOverLifetime,
        SizeOverLifetime,
        SpeedOverLifetime,
        RotationOverLifetime,
        GlowOverLifetime,
        ForceGravity,
        ForceWind,
        ForceTurbulence,
        ForceVortex,
        ForceDrag,
        ForcePointAttractor,
        ForceCurlNoise,
        ForceKillVolume,
        Shape
    };

    inline bool isModifierNode(VFXNodeType type)
    {
        return type == VFXNodeType::ColorOverLifetime ||
               type == VFXNodeType::SizeOverLifetime ||
               type == VFXNodeType::SpeedOverLifetime ||
               type == VFXNodeType::RotationOverLifetime ||
               type == VFXNodeType::GlowOverLifetime;
    }

    inline bool isForceNode(VFXNodeType type)
    {
        return type == VFXNodeType::ForceGravity ||
               type == VFXNodeType::ForceWind ||
               type == VFXNodeType::ForceTurbulence ||
               type == VFXNodeType::ForceVortex ||
               type == VFXNodeType::ForceDrag ||
               type == VFXNodeType::ForcePointAttractor ||
               type == VFXNodeType::ForceCurlNoise ||
               type == VFXNodeType::ForceKillVolume;
    }

    inline bool isShapeNode(VFXNodeType type)
    {
        return type == VFXNodeType::Shape;
    }

    inline bool hasShapeInputPin(VFXNodeType type)
    {
        return type == VFXNodeType::Emitter;
    }

    inline bool hasInputPin(VFXNodeType type)
    {
        return type != VFXNodeType::Emitter && type != VFXNodeType::Shape;
    }

    inline bool hasOutputPin(VFXNodeType type)
    {
        return type != VFXNodeType::OutSystem;
    }

    struct VFXProperty
    {
        std::string name;
        VFXPropertyType type = VFXPropertyType::Float;
        VFXPropertyValue value;
        float min = 0.0f;
        float max = 1.0f;
    };

    struct VFXNode
    {
        uint32_t id = 0;
        VFXNodeType type = VFXNodeType::Emitter;
        std::string name;
        glm::vec2 position{0.0f, 0.0f};

        std::map<std::string, VFXProperty> properties;
    };

    struct VFXNodeLink
    {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0;
        uint32_t targetNodeId = 0;
        std::string sourcePin;
        std::string targetPin;
    };

    struct VFXGraph
    {
        std::vector<VFXNode> nodes;
        std::vector<VFXNodeLink> links;
        uint32_t nextNodeId = 1;
        uint32_t nextLinkId = 1;

        VFXNode* findNode(uint32_t nodeId);
        const VFXNode* findNode(uint32_t nodeId) const;
        const VFXNode* findEmitterNode() const;
        const VFXNode* findOutSystemNode() const;

        bool isValid() const;
        std::string getValidationError() const;
    };

    struct VFXData
    {
        std::string uuid;
        std::string name;
        std::string version;
        VFXGraph graph;

        // VK-1453 (Phase 4) — all additive with neutral defaults so existing
        // .vfVFX assets load and behave byte-identically.
        VFXBounds bounds;             // explicit/derived bounds for cull + viz
        VFXScalability scalability;   // per-quality-tier scalability profile (disabled by default)
        bool cullEligible = false;    // opt-in: allow pre-spawn distance+frustum culling
    };

    namespace EmitterDefaults
    {
        inline constexpr float SPAWN_RATE = 10.0f;
        inline constexpr float LIFETIME = 2.0f;
        inline constexpr float START_SIZE = 1.0f;
        inline constexpr float START_SPEED = 1.0f;
        inline constexpr bool LOOPING = true;
        inline constexpr float INHERIT_VELOCITY_RATIO = 0.0f;
        inline constexpr float SIZE_VARIANCE = 0.0f;
        inline constexpr float LIFETIME_VARIANCE = 0.0f;
        inline constexpr float SPEED_VARIANCE = 0.0f;
        inline constexpr float ROTATION_VARIANCE_DEGREES = 0.0f;
        inline constexpr float ANGULAR_VELOCITY_VARIANCE_DEGREES = 0.0f;
        inline constexpr float COLOR_VALUE_VARIANCE = 0.0f;
        inline constexpr float ALPHA_VARIANCE = 0.0f;

        inline constexpr int FLIPBOOK_ROWS = 1;
        inline constexpr int FLIPBOOK_COLUMNS = 1;
        inline constexpr float FLIPBOOK_FRAME_RATE = 0.0f;
        inline constexpr bool FLIPBOOK_RANDOM_START = false;
        inline constexpr bool FLIPBOOK_FRAME_BLEND = false;

        // Rendering
        inline constexpr float ALPHA_CLIP_THRESHOLD = 0.1f;
        inline constexpr bool ADDITIVE_BLEND = false;

        inline constexpr int RENDER_MODE = 0;
        inline constexpr float SOFT_PARTICLE_DISTANCE = 0.0f;
        inline constexpr float STRETCH_MULTIPLIER = 1.0f;

        inline constexpr int MAX_TRAIL_POINTS = 64;
        inline constexpr float RIBBON_WIDTH = 1.0f;
        inline constexpr float RIBBON_MIN_DISTANCE = 0.1f;

        inline constexpr float UV_SCROLL_SPEED_U = 0.0f;
        inline constexpr float UV_SCROLL_SPEED_V = 0.0f;

        // Lighting
        inline constexpr float LIGHTING_INFLUENCE = 0.0f;
        inline constexpr int NORMAL_MODE = 0;              // 0 = sphere, 1 = view-aligned, 2 = mesh
        inline constexpr float AMBIENT_AMOUNT = 0.3f;

        // Proxy light emission
        inline constexpr bool LIGHT_EMISSION_ENABLED = false;
        inline constexpr float LIGHT_EMISSION_INTENSITY = 5.0f;
        inline constexpr float LIGHT_EMISSION_RADIUS = 10.0f;

        // Collision
        inline constexpr bool COLLISION_ENABLED = false;
        inline constexpr float COLLISION_BOUNCE = 0.5f;
        inline constexpr float COLLISION_FRICTION = 0.1f;
        inline constexpr float COLLISION_LIFETIME_LOSS = 0.0f;

        // Events
        inline constexpr bool EVENT_ON_SPAWN_ENABLED = false;
        inline constexpr bool EVENT_ON_DEATH_ENABLED = false;
        inline constexpr bool EVENT_ON_COLLISION_ENABLED = false;
        inline constexpr bool EVENT_ON_LIFETIME_THRESHOLD_ENABLED = false;
        inline constexpr float EVENT_LIFETIME_THRESHOLD = 0.5f;
    }

    namespace ModifierDefaults
    {
        inline const glm::vec4 COLOR_START{1.0f, 1.0f, 1.0f, 1.0f};
        inline const glm::vec4 COLOR_END{1.0f, 1.0f, 1.0f, 0.0f};

        inline constexpr float SIZE_START_MULTIPLIER = 1.0f;
        inline constexpr float SIZE_END_MULTIPLIER = 0.0f;

        inline constexpr float SPEED_START_MULTIPLIER = 1.0f;
        inline constexpr float SPEED_END_MULTIPLIER = 0.5f;

        inline constexpr float ANGULAR_VELOCITY = 0.0f;
    }

    namespace ForceDefaults
    {
        inline const glm::vec3 GRAVITY_DIRECTION{0.0f, -1.0f, 0.0f};
        inline constexpr float GRAVITY_STRENGTH = 9.81f;

        inline const glm::vec3 WIND_DIRECTION{1.0f, 0.0f, 0.0f};
        inline constexpr float WIND_STRENGTH = 1.0f;
        inline constexpr float WIND_NOISE_STRENGTH = 0.0f;
        inline constexpr float WIND_NOISE_FREQUENCY = 1.0f;

        inline constexpr float TURBULENCE_STRENGTH = 1.0f;
        inline constexpr float TURBULENCE_FREQUENCY = 1.0f;
        inline constexpr float TURBULENCE_SCROLL_SPEED = 0.0f;
        inline constexpr int TURBULENCE_OCTAVES = 1;

        inline const glm::vec3 VORTEX_AXIS{0.0f, 1.0f, 0.0f};
        inline const glm::vec3 VORTEX_CENTER{0.0f, 0.0f, 0.0f};
        inline constexpr float VORTEX_STRENGTH = 1.0f;
        inline constexpr float VORTEX_RADIAL_PULL = 0.0f;

        inline constexpr float DRAG_LINEAR_COEFF = 1.0f;
        inline constexpr float DRAG_QUADRATIC_COEFF = 0.0f;

        inline const glm::vec3 ATTRACTOR_POSITION{0.0f, 0.0f, 0.0f};
        inline constexpr float ATTRACTOR_STRENGTH = 5.0f;
        inline constexpr float ATTRACTOR_RADIUS = 10.0f;
        inline constexpr float ATTRACTOR_FALLOFF = 1.0f;
        inline constexpr bool ATTRACTOR_KILL_AT_CENTER = false;

        inline constexpr float CURLNOISE_STRENGTH = 1.0f;
        inline constexpr float CURLNOISE_FREQUENCY = 1.0f;
        inline constexpr float CURLNOISE_SCROLL_SPEED = 0.0f;
        inline constexpr int CURLNOISE_OCTAVES = 1;

        inline constexpr const char* KILLVOLUME_SHAPE = "Plane";
        inline const glm::vec3 KILLVOLUME_CENTER{0.0f, 0.0f, 0.0f};
        inline const glm::vec3 KILLVOLUME_NORMAL{0.0f, 1.0f, 0.0f};
        inline constexpr float KILLVOLUME_RADIUS = 1.0f;
        inline const glm::vec3 KILLVOLUME_HALF_EXTENTS{1.0f, 1.0f, 1.0f};
        inline constexpr bool KILLVOLUME_INVERT = false;
    }

    const char* propertyTypeToString(VFXPropertyType type);
    VFXPropertyType stringToPropertyType(const std::string& str);
    const char* nodeTypeToString(VFXNodeType type);
    VFXNodeType stringToNodeType(const std::string& str);
}
