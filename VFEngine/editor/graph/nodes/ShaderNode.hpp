#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include "material/MaterialTypes.hpp"

namespace editor::graph {

    // Forward declaration
    class ShaderNode;

    // Pin connection info for code generation
    struct PinConnection {
        uint32_t nodeId = 0;
        std::string pinName;
        bool isConnected = false;
    };

    // Base class for all shader graph nodes
    class ShaderNodeBase {
    public:
        virtual ~ShaderNodeBase() = default;

        // Node identification
        uint32_t getId() const { return id; }
        void setId(uint32_t nodeId) { id = nodeId; }

        material::NodeType getType() const { return type; }
        const std::string& getName() const { return name; }
        void setName(const std::string& nodeName) { name = nodeName; }

        // Position in editor
        glm::vec2 getPosition() const { return position; }
        void setPosition(const glm::vec2& pos) { position = pos; }

        // Pin definitions
        const std::vector<material::NodePin>& getInputPins() const { return inputPins; }
        const std::vector<material::NodePin>& getOutputPins() const { return outputPins; }

        // Properties
        const std::map<std::string, material::NodeProperty>& getProperties() const { return properties; }
        void setProperty(const std::string& key, const material::NodeProperty& value) { properties[key] = value; }

        // Code generation - returns GLSL code snippet for this node
        // The outputVarPrefix is used to create unique variable names (e.g., "node_3_")
        virtual std::string generateCode(const std::string& outputVarPrefix,
                                        const std::map<std::string, std::string>& inputVarNames) const = 0;

        // Get the output variable name for a specific output pin
        virtual std::string getOutputVarName(const std::string& outputVarPrefix,
                                            const std::string& pinName) const = 0;

        // Get GLSL type for an output pin (e.g., "float", "vec3", "vec4")
        virtual std::string getOutputType(const std::string& pinName) const = 0;

    protected:
        uint32_t id = 0;
        material::NodeType type = material::NodeType::ConstantScalar;
        std::string name;
        glm::vec2 position{ 0.0f };

        std::vector<material::NodePin> inputPins;
        std::vector<material::NodePin> outputPins;
        std::map<std::string, material::NodeProperty> properties;

        // Helper to create input pin
        void addInputPin(const std::string& pinName, material::PinType pinType,
                        std::optional<material::ParameterValue> defaultVal = std::nullopt) {
            material::NodePin pin;
            pin.id = 0;  // Will be assigned by graph
            pin.name = pinName;
            pin.type = pinType;
            pin.kind = material::PinKind::Input;
            pin.defaultValue = defaultVal;
            inputPins.push_back(pin);
        }

        // Helper to create output pin
        void addOutputPin(const std::string& pinName, material::PinType pinType) {
            material::NodePin pin;
            pin.id = 0;  // Will be assigned by graph
            pin.name = pinName;
            pin.type = pinType;
            pin.kind = material::PinKind::Output;
            outputPins.push_back(pin);
        }

        // Helper to get property as specific type
        template<typename T>
        T getPropertyValue(const std::string& key, const T& defaultValue) const {
            auto it = properties.find(key);
            if (it != properties.end()) {
                if (auto* val = std::get_if<T>(&it->second)) {
                    return *val;
                }
            }
            return defaultValue;
        }

        // Helper to format float for GLSL
        static std::string floatToGLSL(float value) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.6f", value);
            return buffer;
        }

        // Helper to format vec2 for GLSL
        static std::string vec2ToGLSL(const glm::vec2& v) {
            return "vec2(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ")";
        }

        // Helper to format vec3 for GLSL
        static std::string vec3ToGLSL(const glm::vec3& v) {
            return "vec3(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ", " + floatToGLSL(v.z) + ")";
        }

        // Helper to format vec4 for GLSL
        static std::string vec4ToGLSL(const glm::vec4& v) {
            return "vec4(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ", " +
                   floatToGLSL(v.z) + ", " + floatToGLSL(v.w) + ")";
        }
    };

    // Factory for creating nodes from type
    class ShaderNodeFactory {
    public:
        static std::unique_ptr<ShaderNodeBase> createNode(material::NodeType type);
        static std::unique_ptr<ShaderNodeBase> createNodeFromData(const material::ShaderNode& data);

        // Initialize a ShaderNode data struct with pins based on its type
        // nextPinId is updated to assign unique IDs to each pin
        static void initializeNode(material::ShaderNode& node, uint32_t& nextPinId);
    };

}
