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

    // VFX Node types - Part 1 only has Emitter and OutSystem
    enum class VFXNodeType : uint8_t
    {
        Emitter, // Start node - particle spawn configuration
        OutSystem // End node - final output of the VFX system
    };

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

    // Type conversion utilities
    const char* propertyTypeToString(VFXPropertyType type);
    VFXPropertyType stringToPropertyType(const std::string& str);
    const char* nodeTypeToString(VFXNodeType type);
    VFXNodeType stringToNodeType(const std::string& str);
}
