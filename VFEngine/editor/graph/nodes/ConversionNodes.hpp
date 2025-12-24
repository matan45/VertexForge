#pragma once
#include "ShaderNode.hpp"
#include <algorithm>

namespace editor::graph {

    // Float to Vec2: broadcasts value to both components
    class FloatToVec2Node : public ShaderNodeBase {
    public:
        FloatToVec2Node() {
            type = material::NodeType::FloatToVec2;
            name = "Float To Vec2";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec2 " + outputVarPrefix + "Result = vec2(" + val + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Float to Vec3: broadcasts value to all components
    class FloatToVec3Node : public ShaderNodeBase {
    public:
        FloatToVec3Node() {
            type = material::NodeType::FloatToVec3;
            name = "Float To Vec3";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec3 " + outputVarPrefix + "Result = vec3(" + val + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Float to Vec4: broadcasts value to all components
    class FloatToVec4Node : public ShaderNodeBase {
    public:
        FloatToVec4Node() {
            type = material::NodeType::FloatToVec4;
            name = "Float To Vec4";

            addInputPin("Value", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec4);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "0.0";
            return "vec4 " + outputVarPrefix + "Result = vec4(" + val + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec4";
        }
    };

    // Vec2 to Float: extracts selected component (X=0, Y=1)
    class Vec2ToFloatNode : public ShaderNodeBase {
    public:
        Vec2ToFloatNode() {
            type = material::NodeType::Vec2ToFloat;
            name = "Vec2 To Float";

            addInputPin("Value", material::PinType::Vec2, glm::vec2(0.0f));
            addOutputPin("Result", material::PinType::Float);

            // Component selector: 0=X, 1=Y
            properties["component"] = 0.0f;
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec2(0.0)";
            int comp = static_cast<int>(getPropertyValue<float>("component", 0.0f));
            const char* components[] = {"x", "y"};
            comp = std::clamp(comp, 0, 1);
            return "float " + outputVarPrefix + "Result = " + val + "." + components[comp] + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec3 to Float: extracts selected component (X=0, Y=1, Z=2)
    class Vec3ToFloatNode : public ShaderNodeBase {
    public:
        Vec3ToFloatNode() {
            type = material::NodeType::Vec3ToFloat;
            name = "Vec3 To Float";

            addInputPin("Value", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Float);

            // Component selector: 0=X, 1=Y, 2=Z
            properties["component"] = 0.0f;
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec3(0.0)";
            int comp = static_cast<int>(getPropertyValue<float>("component", 0.0f));
            const char* components[] = {"x", "y", "z"};
            comp = std::clamp(comp, 0, 2);
            return "float " + outputVarPrefix + "Result = " + val + "." + components[comp] + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec4 to Float: extracts selected component (X=0, Y=1, Z=2, W=3)
    class Vec4ToFloatNode : public ShaderNodeBase {
    public:
        Vec4ToFloatNode() {
            type = material::NodeType::Vec4ToFloat;
            name = "Vec4 To Float";

            addInputPin("Value", material::PinType::Vec4, glm::vec4(0.0f));
            addOutputPin("Result", material::PinType::Float);

            // Component selector: 0=X, 1=Y, 2=Z, 3=W
            properties["component"] = 0.0f;
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec4(0.0)";
            int comp = static_cast<int>(getPropertyValue<float>("component", 0.0f));
            const char* components[] = {"x", "y", "z", "w"};
            comp = std::clamp(comp, 0, 3);
            return "float " + outputVarPrefix + "Result = " + val + "." + components[comp] + ";\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "float";
        }
    };

    // Vec2 to Vec3: appends Z component
    class Vec2ToVec3Node : public ShaderNodeBase {
    public:
        Vec2ToVec3Node() {
            type = material::NodeType::Vec2ToVec3;
            name = "Vec2 To Vec3";

            addInputPin("XY", material::PinType::Vec2, glm::vec2(0.0f));
            addInputPin("Z", material::PinType::Float, 0.0f);
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string xy = inputVarNames.count("XY") ? inputVarNames.at("XY") : "vec2(0.0)";
            std::string z = inputVarNames.count("Z") ? inputVarNames.at("Z") : "0.0";
            return "vec3 " + outputVarPrefix + "Result = vec3(" + xy + ", " + z + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

    // Vec2 to Vec4: appends Z and W components
    class Vec2ToVec4Node : public ShaderNodeBase {
    public:
        Vec2ToVec4Node() {
            type = material::NodeType::Vec2ToVec4;
            name = "Vec2 To Vec4";

            addInputPin("XY", material::PinType::Vec2, glm::vec2(0.0f));
            addInputPin("Z", material::PinType::Float, 0.0f);
            addInputPin("W", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Vec4);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string xy = inputVarNames.count("XY") ? inputVarNames.at("XY") : "vec2(0.0)";
            std::string z = inputVarNames.count("Z") ? inputVarNames.at("Z") : "0.0";
            std::string w = inputVarNames.count("W") ? inputVarNames.at("W") : "1.0";
            return "vec4 " + outputVarPrefix + "Result = vec4(" + xy + ", " + z + ", " + w + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec4";
        }
    };

    // Vec3 to Vec2: extracts XY components
    class Vec3ToVec2Node : public ShaderNodeBase {
    public:
        Vec3ToVec2Node() {
            type = material::NodeType::Vec3ToVec2;
            name = "Vec3 To Vec2";

            addInputPin("Value", material::PinType::Vec3, glm::vec3(0.0f));
            addOutputPin("Result", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec3(0.0)";
            return "vec2 " + outputVarPrefix + "Result = " + val + ".xy;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Vec3 to Vec4: appends W component
    class Vec3ToVec4Node : public ShaderNodeBase {
    public:
        Vec3ToVec4Node() {
            type = material::NodeType::Vec3ToVec4;
            name = "Vec3 To Vec4";

            addInputPin("RGB", material::PinType::Vec3, glm::vec3(0.0f));
            addInputPin("W", material::PinType::Float, 1.0f);
            addOutputPin("Result", material::PinType::Vec4);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string rgb = inputVarNames.count("RGB") ? inputVarNames.at("RGB") : "vec3(0.0)";
            std::string w = inputVarNames.count("W") ? inputVarNames.at("W") : "1.0";
            return "vec4 " + outputVarPrefix + "Result = vec4(" + rgb + ", " + w + ");\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec4";
        }
    };

    // Vec4 to Vec2: extracts XY components
    class Vec4ToVec2Node : public ShaderNodeBase {
    public:
        Vec4ToVec2Node() {
            type = material::NodeType::Vec4ToVec2;
            name = "Vec4 To Vec2";

            addInputPin("Value", material::PinType::Vec4, glm::vec4(0.0f));
            addOutputPin("Result", material::PinType::Vec2);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec4(0.0)";
            return "vec2 " + outputVarPrefix + "Result = " + val + ".xy;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec2";
        }
    };

    // Vec4 to Vec3: drops W component
    class Vec4ToVec3Node : public ShaderNodeBase {
    public:
        Vec4ToVec3Node() {
            type = material::NodeType::Vec4ToVec3;
            name = "Vec4 To Vec3";

            addInputPin("Value", material::PinType::Vec4, glm::vec4(0.0f));
            addOutputPin("Result", material::PinType::Vec3);
        }

        std::string generateCode(const std::string& outputVarPrefix,
                                const std::map<std::string, std::string>& inputVarNames) const override {
            std::string val = inputVarNames.count("Value") ? inputVarNames.at("Value") : "vec4(0.0)";
            return "vec3 " + outputVarPrefix + "Result = " + val + ".xyz;\n";
        }

        std::string getOutputVarName(const std::string& outputVarPrefix,
                                    const std::string& /*pinName*/) const override {
            return outputVarPrefix + "Result";
        }

        std::string getOutputType(const std::string& /*pinName*/) const override {
            return "vec3";
        }
    };

}
