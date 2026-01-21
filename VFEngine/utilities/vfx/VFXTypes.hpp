#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <glm/glm.hpp>

namespace vfx
{
    // Property types supported by VFX nodes
    enum class VFXPropertyType : uint8_t
    {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Color, // Same as Vec4 but with color picker UI
        Int,
        Bool,
        String // For file paths (textures)
    };

    // Property value variant - holds any supported property type
    using VFXPropertyValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4, int32_t, bool, std::string>;

    // VFX Node types
    enum class VFXNodeType : uint8_t
    {
        Emitter, // Start node - particle spawn configuration
        OutSystem, // End node - final output of the VFX system
        // Modifier nodes (VK-238)
        ColorOverLifetime, // Interpolates color from start to end over particle lifetime
        SizeOverLifetime, // Scales size from start to end multiplier over particle lifetime
        SpeedOverLifetime, // Scales velocity from start to end multiplier over particle lifetime
        RotationOverLifetime, // Applies angular velocity for spinning particles
        // Force nodes (VK-239)
        ForceGravity,    // Constant directional force (default: downward)
        ForceWind,       // Directional force with optional noise
        ForceTurbulence, // Perlin noise-based chaotic movement
        ForceVortex,     // Spiral force around an axis
        // Shape nodes (VK-240)
        Shape            // Emission shape configuration (connects to Emitter's shape input)
    };

    // Helper function to check if a node type is a modifier
    inline bool isModifierNode(VFXNodeType type)
    {
        return type == VFXNodeType::ColorOverLifetime ||
               type == VFXNodeType::SizeOverLifetime ||
               type == VFXNodeType::SpeedOverLifetime ||
               type == VFXNodeType::RotationOverLifetime;
    }

    // Helper function to check if a node type is a force (VK-239)
    inline bool isForceNode(VFXNodeType type)
    {
        return type == VFXNodeType::ForceGravity ||
               type == VFXNodeType::ForceWind ||
               type == VFXNodeType::ForceTurbulence ||
               type == VFXNodeType::ForceVortex;
    }

    // Helper function to check if a node type is a shape (VK-240)
    inline bool isShapeNode(VFXNodeType type)
    {
        return type == VFXNodeType::Shape;
    }

    // Check if node type has a shape input pin (only Emitter has this)
    inline bool hasShapeInputPin(VFXNodeType type)
    {
        return type == VFXNodeType::Emitter;
    }

    // Check if node type has a data flow input pin (all except Emitter and Shape)
    inline bool hasInputPin(VFXNodeType type)
    {
        return type != VFXNodeType::Emitter && type != VFXNodeType::Shape;
    }

    // Check if node type has an output pin (all except OutSystem)
    inline bool hasOutputPin(VFXNodeType type)
    {
        return type != VFXNodeType::OutSystem;
    }

    // A property definition for a node
    struct VFXProperty
    {
        std::string name;
        VFXPropertyType type = VFXPropertyType::Float;
        VFXPropertyValue value;
        float min = 0.0f; // For numeric types
        float max = 1.0f; // For numeric types
    };

    // VFX Node structure
    struct VFXNode
    {
        uint32_t id = 0;
        VFXNodeType type = VFXNodeType::Emitter;
        std::string name;
        glm::vec2 position{0.0f, 0.0f}; // Node editor position

        // Node-specific properties stored as key-value map
        std::map<std::string, VFXProperty> properties;
    };

    // Connection between nodes
    struct VFXNodeLink
    {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0;
        uint32_t targetNodeId = 0;
        std::string sourcePin; // Output pin name
        std::string targetPin; // Input pin name
    };

    // VFX Graph containing nodes and connections
    struct VFXGraph
    {
        std::vector<VFXNode> nodes;
        std::vector<VFXNodeLink> links;
        uint32_t nextNodeId = 1;
        uint32_t nextLinkId = 1;

        // Helper methods
        VFXNode* findNode(uint32_t nodeId);
        const VFXNode* findNode(uint32_t nodeId) const;
        const VFXNode* findEmitterNode() const;
        const VFXNode* findOutSystemNode() const;

        // Graph validation (VK-85: OutSystem validates execution)
        bool isValid() const;
        std::string getValidationError() const;
    };

    // Root VFX asset data
    struct VFXData
    {
        std::string uuid;
        std::string name;
        std::string version;
        VFXGraph graph;
    };

    // Default values for Emitter node properties
    namespace EmitterDefaults
    {
        inline constexpr float SPAWN_RATE = 10.0f; // particles per second
        inline constexpr float LIFETIME = 2.0f; // seconds
        inline constexpr float START_SIZE = 1.0f; // scale
        inline constexpr float START_SPEED = 1.0f; // units per second
        inline constexpr bool LOOPING = true; // whether VFX loops
    }

    // Default values for Modifier nodes (VK-238)
    namespace ModifierDefaults
    {
        // Color Over Lifetime
        inline const glm::vec4 COLOR_START{1.0f, 1.0f, 1.0f, 1.0f};
        inline const glm::vec4 COLOR_END{1.0f, 1.0f, 1.0f, 0.0f}; // Fade out alpha

        // Size Over Lifetime
        inline constexpr float SIZE_START_MULTIPLIER = 1.0f;
        inline constexpr float SIZE_END_MULTIPLIER = 0.0f; // Shrink to nothing

        // Speed Over Lifetime
        inline constexpr float SPEED_START_MULTIPLIER = 1.0f;
        inline constexpr float SPEED_END_MULTIPLIER = 0.5f; // Slow down

        // Rotation Over Lifetime
        inline constexpr float ANGULAR_VELOCITY = 0.0f; // degrees per second
    }

    // Default values for Force nodes (VK-239)
    namespace ForceDefaults
    {
        // Gravity
        inline const glm::vec3 GRAVITY_DIRECTION{0.0f, -1.0f, 0.0f};
        inline constexpr float GRAVITY_STRENGTH = 9.81f;

        // Wind
        inline const glm::vec3 WIND_DIRECTION{1.0f, 0.0f, 0.0f};
        inline constexpr float WIND_STRENGTH = 1.0f;
        inline constexpr float WIND_NOISE_STRENGTH = 0.0f;
        inline constexpr float WIND_NOISE_FREQUENCY = 1.0f;

        // Turbulence
        inline constexpr float TURBULENCE_STRENGTH = 1.0f;
        inline constexpr float TURBULENCE_FREQUENCY = 1.0f;
        inline constexpr float TURBULENCE_SCROLL_SPEED = 0.0f;
        inline constexpr int TURBULENCE_OCTAVES = 1;

        // Vortex
        inline const glm::vec3 VORTEX_AXIS{0.0f, 1.0f, 0.0f};
        inline const glm::vec3 VORTEX_CENTER{0.0f, 0.0f, 0.0f};
        inline constexpr float VORTEX_STRENGTH = 1.0f;
        inline constexpr float VORTEX_RADIAL_PULL = 0.0f;
    }

    // Type conversion utilities
    const char* propertyTypeToString(VFXPropertyType type);
    VFXPropertyType stringToPropertyType(const std::string& str);
    const char* nodeTypeToString(VFXNodeType type);
    VFXNodeType stringToNodeType(const std::string& str);
}
