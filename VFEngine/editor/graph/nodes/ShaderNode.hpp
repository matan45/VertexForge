#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include "material/MaterialTypes.hpp"

namespace editor::graph {

    class ShaderNodeBase {
    public:
        virtual ~ShaderNodeBase() = default;

        uint32_t getId() const { return id; }
        void setId(uint32_t nodeId) { id = nodeId; }

        material::NodeType getType() const { return type; }
        const std::string& getName() const { return name; }
        void setName(const std::string& nodeName) { name = nodeName; }

        glm::vec2 getPosition() const { return position; }
        void setPosition(const glm::vec2& pos) { position = pos; }

        const std::vector<material::NodePin>& getInputPins() const { return inputPins; }
        const std::vector<material::NodePin>& getOutputPins() const { return outputPins; }

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

        void addOutputPin(const std::string& pinName, material::PinType pinType) {
            material::NodePin pin;
            pin.id = 0;  // Will be assigned by graph
            pin.name = pinName;
            pin.type = pinType;
            pin.kind = material::PinKind::Output;
            outputPins.push_back(pin);
        }

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

        static std::string floatToGLSL(float value) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.6f", value);
            return buffer;
        }

        static std::string vec2ToGLSL(const glm::vec2& v) {
            return "vec2(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ")";
        }

        static std::string vec3ToGLSL(const glm::vec3& v) {
            return "vec3(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ", " + floatToGLSL(v.z) + ")";
        }

        static std::string vec4ToGLSL(const glm::vec4& v) {
            return "vec4(" + floatToGLSL(v.x) + ", " + floatToGLSL(v.y) + ", " +
                   floatToGLSL(v.z) + ", " + floatToGLSL(v.w) + ")";
        }
    };

    class ShaderNodeFactory {
    public:
        static std::unique_ptr<ShaderNodeBase> createNode(material::NodeType type);
        static std::unique_ptr<ShaderNodeBase> createNodeFromData(const material::ShaderNode& data);

        // Initialize a ShaderNode data struct with pins based on its type
        // nextPinId is updated to assign unique IDs to each pin
        static void initializeNode(material::ShaderNode& node, uint32_t& nextPinId);
    };

}
