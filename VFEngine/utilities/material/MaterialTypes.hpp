#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <optional>
#include <cstdint>

namespace material {

    // Material format version for serialization compatibility
    constexpr const char* MATERIAL_FORMAT_VERSION = "1.1";
    constexpr const char* MATERIAL_FORMAT_VERSION_LEGACY = "1.0";

    // Maximum number of textures per material (expanded for ORM packing and additional maps)
    constexpr int MAX_MATERIAL_TEXTURES = 16;

    // Texture slot indices for the material system
    // Supports both packed ORM workflow and legacy individual textures
    enum class TextureSlot : uint8_t {
        // Core PBR textures
        Albedo = 0,             // RGB color, A for opacity
        Normal = 1,             // Tangent-space normal map
        ORM = 2,                // Packed: R=AO, G=Roughness, B=Metallic

        // Legacy individual textures (backward compatibility)
        Metallic = 3,           // Individual metallic map
        Roughness = 4,          // Individual roughness map
        AO = 5,                 // Individual ambient occlusion map

        // Additional textures
        Emission = 6,           // RGB emission color
        Height = 7,             // Height/displacement map
        DetailNormal = 8,       // Secondary normal map for detail
        DetailAlbedo = 9,       // Secondary albedo for detail
        Subsurface = 10,        // Subsurface scattering
        Anisotropy = 11,        // Anisotropic direction/strength
        Clearcoat = 12,         // Clearcoat layer
        ClearcoatNormal = 13,   // Clearcoat normal map

        // Reserved for future use
        Reserved1 = 14,
        Reserved2 = 15,

        Count = 16
    };

    // Helper to convert TextureSlot to index
    constexpr int toIndex(TextureSlot slot) {
        return static_cast<int>(slot);
    }

    // Parameter types for material properties
    enum class ParameterType : uint8_t {
        Scalar,
        Vec2,
        Vec3,
        Vec4,
        Color  // Same as Vec4 but with color picker UI
    };

    // Value variant for material parameters
    using ParameterValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4>;

    // Material parameter definition
    struct MaterialParameter {
        ParameterType type = ParameterType::Scalar;
        std::string name;
        ParameterValue value;
        float min = 0.0f;
        float max = 1.0f;

        MaterialParameter() = default;
        MaterialParameter(const std::string& paramName, float val, float minVal = 0.0f, float maxVal = 1.0f)
            : type(ParameterType::Scalar), name(paramName), value(val), min(minVal), max(maxVal) {}
        MaterialParameter(const std::string& paramName, const glm::vec2& val)
            : type(ParameterType::Vec2), name(paramName), value(val) {}
        MaterialParameter(const std::string& paramName, const glm::vec3& val, bool isColor = false)
            : type(isColor ? ParameterType::Color : ParameterType::Vec3), name(paramName), value(glm::vec4(val, 1.0f)) {}
        MaterialParameter(const std::string& paramName, const glm::vec4& val)
            : type(ParameterType::Vec4), name(paramName), value(val) {}
    };

    // Pin types for shader graph nodes
    enum class PinType : uint8_t {
        Float,
        Vec2,
        Vec3,
        Vec4,
        Texture2D  // For future texture support
    };

    // Pin direction
    enum class PinKind : uint8_t {
        Input,
        Output
    };

    // Node pin definition
    struct NodePin {
        uint32_t id = 0;
        std::string name;
        PinType type = PinType::Float;
        PinKind kind = PinKind::Input;
        std::optional<ParameterValue> defaultValue;  // Default value if not connected
    };

    // Shader node types
    enum class NodeType : uint8_t {
        // Output
        PBROutput,

        // Constants
        ConstantScalar,
        ConstantVec2,
        ConstantVec3,
        ConstantColor,

        // Math operations
        Add,
        Subtract,
        Multiply,
        Divide,
        Power,
        Lerp,
        Clamp,
        Saturate,
        OneMinus,
        Abs,
        Floor,
        Ceil,
        Fract,
        Sin,
        Cos,
        Dot,
        Cross,
        Normalize,
        Length,

        // Mix/Blend
        MixColor,       // Mix two Vec3 colors by alpha factor

        // Utilities
        MakeVec2,
        MakeVec3,
        MakeVec4,
        SplitVec2,
        SplitVec3,
        SplitVec4,
        Fresnel,

        // Input (vertex data)
        VertexNormal,
        VertexUV,
        Time,

        // Texture
        TextureSample
    };

    // Node property variant (for node-specific settings)
    using NodeProperty = std::variant<float, glm::vec2, glm::vec3, glm::vec4, std::string>;

    // Shader graph node
    struct ShaderNode {
        uint32_t id = 0;
        NodeType type = NodeType::ConstantScalar;
        glm::vec2 position{ 0.0f };
        std::string name;  // Display name

        // Node-specific properties (e.g., constant values, parameter names)
        std::map<std::string, NodeProperty> properties;

        // Pins (populated based on node type)
        std::vector<NodePin> inputs;
        std::vector<NodePin> outputs;
    };

    // Link between nodes
    struct NodeLink {
        uint32_t id = 0;
        uint32_t sourceNodeId = 0;
        uint32_t targetNodeId = 0;
        std::string sourcePin;
        std::string targetPin;
    };

    // Shader graph containing all nodes and links
    struct ShaderGraph {
        std::vector<ShaderNode> nodes;
        std::vector<NodeLink> links;
        uint32_t nextNodeId = 1;
        uint32_t nextLinkId = 1;
        uint32_t nextPinId = 1;

        // Find the PBR output node (should always exist)
        const ShaderNode* findOutputNode() const {
            for (const auto& node : nodes) {
                if (node.type == NodeType::PBROutput) {
                    return &node;
                }
            }
            return nullptr;
        }

        // Find node by ID
        ShaderNode* findNode(uint32_t nodeId) {
            for (auto& node : nodes) {
                if (node.id == nodeId) {
                    return &node;
                }
            }
            return nullptr;
        }

        const ShaderNode* findNode(uint32_t nodeId) const {
            for (const auto& node : nodes) {
                if (node.id == nodeId) {
                    return &node;
                }
            }
            return nullptr;
        }
    };

    // Blend modes for materials
    enum class BlendMode : uint8_t {
        Opaque,
        Masked,
        Translucent
    };

    // Complete material data
    struct MaterialData {
        std::string uuid;
        std::string name;
        BlendMode blendMode = BlendMode::Opaque;

        // Shader graph
        ShaderGraph graph;

        // Exposed parameters (for runtime modification, not serialized to file)
        std::map<std::string, MaterialParameter> parameters;

        // Cached generated shader code
        std::string cachedVertexShader;
        std::string cachedFragmentShader;

        // Check if shader needs regeneration
        bool needsRecompile = true;
    };

}
