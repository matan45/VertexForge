#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <glm/glm.hpp>
#include "VFXCurveTypes.hpp"

namespace vfx
{
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
               type == VFXNodeType::ForceVortex;
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
    };

    namespace EmitterDefaults
    {
        inline constexpr float SPAWN_RATE = 10.0f;
        inline constexpr float LIFETIME = 2.0f;
        inline constexpr float START_SIZE = 1.0f;
        inline constexpr float START_SPEED = 1.0f;
        inline constexpr bool LOOPING = true;

        // Flipbook defaults (VK-493)
        inline constexpr int FLIPBOOK_ROWS = 1;
        inline constexpr int FLIPBOOK_COLUMNS = 1;
        inline constexpr float FLIPBOOK_FRAME_RATE = 0.0f;
        inline constexpr bool FLIPBOOK_RANDOM_START = false;

        // Rendering
        inline constexpr float ALPHA_CLIP_THRESHOLD = 0.1f;
        inline constexpr bool ADDITIVE_BLEND = false;

        // Render mode & soft particles (VK-494)
        inline constexpr int RENDER_MODE = 0;
        inline constexpr float SOFT_PARTICLE_DISTANCE = 0.0f;
        inline constexpr float STRETCH_MULTIPLIER = 1.0f;

        // Ribbon (VK-624)
        inline constexpr int MAX_TRAIL_POINTS = 64;
        inline constexpr float RIBBON_WIDTH = 1.0f;
        inline constexpr float RIBBON_MIN_DISTANCE = 0.1f;

        // UV Scrolling (VK-623)
        inline constexpr float UV_SCROLL_SPEED_U = 0.0f;
        inline constexpr float UV_SCROLL_SPEED_V = 0.0f;
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
    }

    const char* propertyTypeToString(VFXPropertyType type);
    VFXPropertyType stringToPropertyType(const std::string& str);
    const char* nodeTypeToString(VFXNodeType type);
    VFXNodeType stringToNodeType(const std::string& str);
}
